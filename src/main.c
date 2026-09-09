/*==================================================================================================
 *   Copyright 2026 NXP
 *
 *   NXP Proprietary. This software is owned or controlled by NXP and may only be
 *   used strictly in accordance with the applicable license terms. By expressly
 *   accepting such terms or by downloading, installing, activating and/or otherwise
 *   using the software, you are agreeing that you have read, and that you agree to
 *   comply with and are bound by, such license terms. If you do not agree to be
 *   bound by the applicable license terms, then you may not retain, install,
 *   activate or otherwise use the software.
 ==================================================================================================*/

/**
 * @file    main.c
 * @brief   Application entry point for the FRDM-A-S32K344 HTTP server example,
 *          combined with a periodic P3T1750DP temperature read over LPI2C that
 *          is reported on the LPUART console.
 *
 * The Ethernet receive/transmit polling path is preserved exactly as before.
 * On top of it, once every N iterations of the main loop, the P3T1750DP
 * temperature sensor is sampled over LPI2C and the value is printed over
 * LPUART and rendered on the SSD1306 OLED dashboard.
 *
 * The OLED shows a small dashboard: a title, the live temperature, the static
 * IPv4 address of the board and a counter of received Ethernet frames. The
 * temperature and the counter are refreshed on the same tick that drives the
 * console output, so the display never adds I2c traffic of its own.
 *
 * Initialization order:
 *   1. OsIf  - OS abstraction layer (tick counter, etc.)
 *   2. Mcu   - Clock tree, mode configuration
 *   3. EthPhy (optional software reset/pin-strapping)
 *   4. Port  - GPIO/RMII/LPI2C/LPUART pin mux
 *   5. PLL lock and distribution
 *   6. Eth_43_GMAC - GMAC DMA ring setup
 *   7. EthPhy (optional full software init)
 *   8. GMAC set ACTIVE -> link comes up
 *   9. Mcl_Init - enables the FlexIO module (required before the FlexIO I2c
 *      channel can be used by the OLED display)
 *  10. I2c_Init / Uart_Init - temperature sensor, OLED and console
 *  11. SSD1306_Init - OLED controller command sequence
 */

#include "OsIf.h"
#include "Mcu.h"
#include "Port.h"
#include "Dio.h"
#include "Eth_43_GMAC.h"
#include "EthPhy.h"
#include "Eth_Stack.h"
#include "Http_Server.h"
#include "App_Commands.h"
#include "EthIf.h"
#include "CDD_Mcl.h"
#include "CDD_I2c.h"
#include "CDD_Uart.h"
#include "ssd1306.h"
#include "P3T1750.h"
#include <string.h>

/*==================================================================================================
 *   DEFINES
 ==================================================================================================*/

/**
 * @brief  Reload value for the software GMAC watchdog.
 *
 * Approximate loop count that corresponds to ~30 seconds at 160 MHz.
 */
#define GMAC_WATCHDOG_RELOAD  (30000000UL)

/* Logical I2c channel as generated in CDD_I2c_CfgDefines.h (AUTOSAR channel index). */
#define I2C_CHANNEL             I2C_MASTER_0

/* Logical Uart channel index inside Uart_xConfig.Configs[]. */
#define UART_CONFIG_INDEX       0U

/* Timeout in microseconds used for the blocking Uart transfers. */
#define UART_TX_TIMEOUT_US      100000U

/* P3T1750DP 7-bit slave address (0x90 >> 1 == 0x48). */
#define TEMP_SENSOR_ADR         0x48U

/* Pointer register value selecting the temperature result register. */
#define TEMP_REG_ADR            0x00U

/* Number of bytes in the P3T1750DP temperature register. */
#define TEMP_REG_SIZE           2U

/* 12-bit two's complement, 0.0625 degC per LSB. Scale by 625 -> units of 0.0001 degC. */
#define TEMP_LSB_SCALE_1E4      625
#define TEMP_SCALE_1E4          10000

/* How many main-loop iterations between two temperature samples.
 * The Ethernet RX/TX polling keeps running while this counts down. */
#define TEMP_SAMPLE_PERIOD      2000000UL

/*==================================================================================================
 *   THERMOSTAT SETPOINT AND RELAY CONTROL
 ==================================================================================================*/

/* Default setpoint in whole degrees Celsius. */
#define TEMP_SETPOINT_DEFAULT   24
/* Minimum and maximum allowed setpoint values. */
#define TEMP_SETPOINT_MIN       16
#define TEMP_SETPOINT_MAX       35

/* Hysteresis in tenths of a degree (same as 1 degC used in the K118 reference). */
#define TEMP_HYSTERESIS_TENTHS  10

/**
 * @brief   File-scope thermostat setpoint (whole degC).
 * @details Shared between the command handler in main() and Relay_UpdateControl()
 *          which is called from Temp_SampleAndReport().
 */
static sint16 g_TempSetpoint = (sint16)TEMP_SETPOINT_DEFAULT;

/** @brief Relay 1 (heating) active state. */
static boolean g_Rl1Active = FALSE;

/** @brief Relay 2 (cooling) active state. */
static boolean g_Rl2Active = FALSE;

/** @brief Green Led state. */
static boolean g_GreenLed = FALSE;


/*==================================================================================================
 *   OLED DASHBOARD LAYOUT
 *
 * The visible area is 96 x 39 px and the built-in font is 5x8 with a 6 px
 * advance, so a full row holds 16 characters. The rows below are chosen so the
 * three value fields line up under a title and separator.
 * Row layout: TITLE / TEMP (sensor) / SET (thermostat setpoint) / RX (frame counter)
 ==================================================================================================*/

/* Y coordinate of each text row. */
#define OLED_ROW_TITLE          0
#define OLED_ROW_TEMP           11
#define OLED_ROW_SET            21
#define OLED_ROW_RX             31

/* Y coordinate of the horizontal rule drawn under the title. */
#define OLED_SEPARATOR_Y        9

/* X coordinate of the labels and of the value fields that follow them. */
#define OLED_LABEL_X            1
#define OLED_VALUE_X            14

/* Width in pixels of a value field, used to erase it before a redraw. */
#define OLED_VALUE_W            82

/* Font metrics of the built-in 5x8 bitmap font: 5 px glyph plus 1 px gap. */
#define OLED_CHAR_ADVANCE       6
#define OLED_CHAR_HEIGHT        8

/* Temperature scaling used for the single decimal shown on the display.
 * TEMP_LSB_SCALE_1E4 yields units of 0.0001 degC, so dividing by 1000 gives
 * units of 0.1 degC. */
#define TEMP_SCALE_1E1_DIV      1000
#define TEMP_SCALE_1E1          10



/*==================================================================================================
 *   TEMPERATURE / CONSOLE HELPERS
 ==================================================================================================*/

/**
 * @brief Send a null-terminated string over UART and block until it completes.
 */
static void Uart_SendBlocking(const char *pMsg)
{
    (void)Uart_SyncSend(Uart_xConfig.Configs[UART_CONFIG_INDEX]->UartChannelId,
                        (const uint8 *)pMsg,
                        (uint32)strlen(pMsg),
                        UART_TX_TIMEOUT_US);
}

/**
 * @brief Convert a raw P3T1750DP register pair into a signed 12-bit temperature code.
 */
static sint16 Temp_DecodeRaw(const uint8 *pRegData)
{
    uint16 RawCode;
    sint16 Result;

    RawCode = (uint16)(((uint16)pRegData[0] << 4U) | ((uint16)pRegData[1] >> 4U));
    RawCode = (uint16)(RawCode & 0x0FFFU);

    if (0U != (RawCode & 0x0800U))
    {
        Result = (sint16)(RawCode | 0xF000U);
    }
    else
    {
        Result = (sint16)RawCode;
    }

    return Result;
}

/**
 * @brief Read the P3T1750DP temperature register.
 * @return E_OK when both the pointer write and the data read succeeded.
 */
static Std_ReturnType Temp_ReadRegister(uint8 *pRegData)
{
    static uint8 RegAddr = TEMP_REG_ADR;
    Std_ReturnType Status;

    I2c_RequestType RequestSend = {0};
    I2c_RequestType RequestReceive = {0};

    /* Phase 1: write the pointer register, then hold the bus with a repeated start. */
    RequestSend.SlaveAddress           = TEMP_SENSOR_ADR;
    RequestSend.BitsSlaveAddressSize   = FALSE;
    RequestSend.BufferSize             = 1U;
    RequestSend.DataBuffer             = &RegAddr;
    RequestSend.DataDirection          = I2C_SEND_DATA;
    RequestSend.ExpectNack             = FALSE;
    RequestSend.RepeatedStart          = TRUE;

    /* Phase 2: read the two temperature bytes and close the transfer with a stop. */
    RequestReceive.SlaveAddress         = TEMP_SENSOR_ADR;
    RequestReceive.BitsSlaveAddressSize = FALSE;
    RequestReceive.BufferSize           = TEMP_REG_SIZE;
    RequestReceive.DataBuffer           = pRegData;
    RequestReceive.DataDirection        = I2C_RECEIVE_DATA;
    RequestReceive.ExpectNack           = FALSE;
    RequestReceive.RepeatedStart        = FALSE;

    Status = I2c_SyncTransmit(I2C_CHANNEL, &RequestSend);
    if (E_OK == Status)
    {
        Status = I2c_SyncTransmit(I2C_CHANNEL, &RequestReceive);
    }

    return Status;
}

/**
 * @brief Append an unsigned decimal value to a string, optionally zero padded.
 */
static uint32 Temp_AppendUint(char *pBuffer, uint32 Index, uint32 Value, uint32 MinDigits)
{
    char Digits[10];
    uint32 Count = 0U;
    uint32 Position = Index;

    do
    {
        Digits[Count] = (char)('0' + (char)(Value % 10U));
        Value = Value / 10U;
        Count++;
    }
    while ((Value > 0U) && (Count < (uint32)sizeof(Digits)));

    while (MinDigits > Count)
    {
        pBuffer[Position] = '0';
        Position++;
        MinDigits--;
    }

    while (Count > 0U)
    {
        Count--;
        pBuffer[Position] = Digits[Count];
        Position++;
    }

    return Position;
}

/**
 * @brief Format a temperature code as a fixed point decimal string.
 */
static void Temp_Format(char *pBuffer, sint16 RawCode)
{
    static const char Prefix[] = "Temp: ";
    sint32 Scaled;
    uint32 Magnitude;
    uint32 Index;

    Scaled = (sint32)RawCode * TEMP_LSB_SCALE_1E4 - (sint32)(MCU_TEMP_OFFSET * (float)TEMP_SCALE_1E4);

    Index = (uint32)strlen(Prefix);
    (void)memcpy(pBuffer, Prefix, (size_t)Index);

    if (Scaled < 0)
    {
        pBuffer[Index] = '-';
        Index++;
        Magnitude = (uint32)(-Scaled);
    }
    else
    {
        Magnitude = (uint32)Scaled;
    }

    Index = Temp_AppendUint(pBuffer, Index, Magnitude / (uint32)TEMP_SCALE_1E4, 1U);
    pBuffer[Index] = '.';
    Index++;
    Index = Temp_AppendUint(pBuffer, Index, Magnitude % (uint32)TEMP_SCALE_1E4, 4U);

    pBuffer[Index] = ' ';
    Index++;
    pBuffer[Index] = 'C';
    Index++;
    pBuffer[Index] = '\r';
    Index++;
    pBuffer[Index] = '\n';
    Index++;
    pBuffer[Index] = '\0';
}

/*==================================================================================================
 *   OLED DASHBOARD
 ==================================================================================================*/

/**
 * @brief Draw the static parts of the dashboard into the frame buffer.
 * @details Renders the title, the separator rule and the row labels. These
 *          never change, so they are drawn once during start-up and the
 *          periodic refresh only repaints the value fields next to them.
 *          The caller is responsible for the SSD1306_UpdateScreen() flush.
 */
static void Oled_DrawDashboard(void)
{
    SSD1306_DrawString(3, OLED_ROW_TITLE, "Ethernet Server", 1, true);
    SSD1306_DrawLine(0, OLED_SEPARATOR_Y, SCREEN_WIDTH - 1, OLED_SEPARATOR_Y, true);

    SSD1306_DrawString(OLED_LABEL_X, OLED_ROW_TEMP, "T:", 1, true);
    SSD1306_DrawString(OLED_LABEL_X, OLED_ROW_SET,  "S:", 1, true);
    SSD1306_DrawString(OLED_LABEL_X, OLED_ROW_RX,   "RX:", 1, true);
}

/**
 * @brief Render the thermostat setpoint field of the dashboard.
 * @param[in] Setpoint  Current setpoint in whole degrees Celsius.
 */
static void Oled_DrawSetpoint(sint16 Setpoint)
{
    char Buffer[8];
    uint32 Index = 0U;
    uint32 Value;

    if (Setpoint < 0)
    {
        Buffer[Index] = '-';
        Index++;
        Value = (uint32)(-Setpoint);
    }
    else
    {
        Value = (uint32)Setpoint;
    }

    Index = Temp_AppendUint(Buffer, Index, Value, 1U);
    Buffer[Index] = ' ';
    Index++;
    Buffer[Index] = 'C';
    Index++;
    Buffer[Index] = '\0';

    SSD1306_FillRect(OLED_VALUE_X, OLED_ROW_SET, OLED_VALUE_W, OLED_CHAR_HEIGHT, false);
    SSD1306_DrawString(OLED_VALUE_X, OLED_ROW_SET, Buffer, 1, true);
}

/**
 * @brief Render the temperature value field of the dashboard.
 * @details Erases the previous value first, then draws the temperature with a
 *          single decimal followed by a 'C' unit, for example "38.4 C". Only
 *          the value rectangle is touched so the label stays intact.
 * @param[in] RawCode  Signed 12-bit temperature code from the P3T1750DP.
 */
static void Oled_DrawTemperature(sint16 RawCode)
{
    char Buffer[16];
    sint32 Tenths;
    uint32 Magnitude;
    uint32 Index = 0U;

    /* Convert to tenths of a degree. The division truncates toward zero, which
     * is accurate enough for a one-decimal readout on a 96 px panel. */
    Tenths = ((sint32)RawCode * TEMP_LSB_SCALE_1E4) / TEMP_SCALE_1E1_DIV - (sint32)(MCU_TEMP_OFFSET * (float)TEMP_SCALE_1E1);

    if (Tenths < 0)
    {
        Buffer[Index] = '-';
        Index++;
        Magnitude = (uint32)(-Tenths);
    }
    else
    {
        Magnitude = (uint32)Tenths;
    }

    Index = Temp_AppendUint(Buffer, Index, Magnitude / (uint32)TEMP_SCALE_1E1, 1U);
    Buffer[Index] = '.';
    Index++;
    Index = Temp_AppendUint(Buffer, Index, Magnitude % (uint32)TEMP_SCALE_1E1, 1U);
    Buffer[Index] = ' ';
    Index++;
    Buffer[Index] = 'C';
    Index++;
    Buffer[Index] = '\0';

    SSD1306_FillRect(OLED_VALUE_X, OLED_ROW_TEMP, OLED_VALUE_W,
                     OLED_CHAR_HEIGHT, false);
    SSD1306_DrawString(OLED_VALUE_X, OLED_ROW_TEMP, Buffer, 1, true);
}

/**
 * @brief Render the received-frame counter field of the dashboard.
 * @details Shows how many frames the GMAC has handed to EthIf_RxIndication.
 *          A value that climbs while the board is pinged is a quick visual
 *          confirmation that the Ethernet receive path is alive.
 */
static void Oled_DrawRxCount(void)
{
    char Buffer[16];
    uint32 Index;

    Index = Temp_AppendUint(Buffer, 0U, EthIf_RxIndications[0], 1U);
    Buffer[Index] = '\0';

    SSD1306_FillRect(OLED_VALUE_X + OLED_CHAR_ADVANCE, OLED_ROW_RX,
                     OLED_VALUE_W - OLED_CHAR_ADVANCE, OLED_CHAR_HEIGHT, false);
    SSD1306_DrawString(OLED_VALUE_X + OLED_CHAR_ADVANCE, OLED_ROW_RX,
                       Buffer, 1, true);
}

/**
 * @brief   Update RL1 (heating) and RL2 (cooling) based on the current
 *          temperature code and the thermostat setpoint.
 *
 * Hysteresis prevents relay chatter around the setpoint:
 *   rawCode < setpointRaw - TEMP_HYSTERESIS_RAW  -> RL1 ON,  RL2 OFF
 *   rawCode > setpointRaw + TEMP_HYSTERESIS_RAW  -> RL2 ON,  RL1 OFF
 *   otherwise                                    -> both OFF (neutral zone)
 *
 * @param[in] RawCode  Signed 12-bit temperature reading from P3T1750DP.
 */
static void Relay_UpdateControl(sint16 RawCode)
{
    /*
     * Convert the 12-bit sensor code to tenths of a degree Celsius.
     * The P3T1750DP resolution is 0.0625 degC/LSB, so:
     *   tenths = RawCode * 0.0625 * 10 = RawCode * 625 / 1000
     * This matches the existing Oled_DrawTemperature() conversion and the
     * approach used in the K118 reference (which works in float degC).
     *
     * Compare against the setpoint (whole degC) scaled to tenths,
     * with 1 degC = 10 tenths of hysteresis on each side.
     */
    sint32 tempTenths      = ((sint32)RawCode * (sint32)TEMP_LSB_SCALE_1E4) / (sint32)TEMP_SCALE_1E1_DIV - (sint32)(MCU_TEMP_OFFSET * (float)TEMP_SCALE_1E1);
    sint32 setpointTenths  = (sint32)g_TempSetpoint * (sint32)TEMP_SCALE_1E1;
    sint32 lowerThreshold  = setpointTenths - (sint32)TEMP_HYSTERESIS_TENTHS;
    sint32 upperThreshold  = setpointTenths + (sint32)TEMP_HYSTERESIS_TENTHS;

    if (tempTenths < lowerThreshold)
    {
        /* Temperature below lower threshold - activate heating relay. */
        if (g_Rl1Active == FALSE)
        {
            g_Rl1Active = TRUE;
            Dio_WriteChannel(DioConf_DioChannel_RL1, STD_HIGH);
        }
        if (g_Rl2Active != FALSE)
        {
            g_Rl2Active = FALSE;
            Dio_WriteChannel(DioConf_DioChannel_RL2, STD_LOW);
        }
    }
    else if (tempTenths > upperThreshold)
    {
        /* Temperature above upper threshold - activate cooling relay. */
        if (g_Rl2Active == FALSE)
        {
            g_Rl2Active = TRUE;
            Dio_WriteChannel(DioConf_DioChannel_RL2, STD_HIGH);
        }
        if (g_Rl1Active != FALSE)
        {
            g_Rl1Active = FALSE;
            Dio_WriteChannel(DioConf_DioChannel_RL1, STD_LOW);
        }
    }
    else
    {
        /* Temperature within neutral zone - deactivate both relays. */
        if (g_Rl1Active != FALSE)
        {
            g_Rl1Active = FALSE;
            Dio_WriteChannel(DioConf_DioChannel_RL1, STD_LOW);
        }
        if (g_Rl2Active != FALSE)
        {
            g_Rl2Active = FALSE;
            Dio_WriteChannel(DioConf_DioChannel_RL2, STD_LOW);
        }
    }
}

/**
 * @brief Sample the temperature sensor once and report it over UART + OLED.
 *        Skips the read while the I2C channel is still busy from a prior transfer.
 */
static void Temp_SampleAndReport(void)
{
    uint8 RegData[TEMP_REG_SIZE];
    char TxBuffer[48];
    sint16 RawCode;
    I2c_StatusType ChannelStatus;

    ChannelStatus = I2c_GetStatus(I2C_CHANNEL);
    if ((I2C_CH_SEND == ChannelStatus) || (I2C_CH_RECEIVE == ChannelStatus))
    {
        /* A transfer is still in progress, try again on the next sample tick. */
        return;
    }

    if (E_OK == Temp_ReadRegister(RegData))
    {
        RawCode = Temp_DecodeRaw(RegData);
        Temp_Format(TxBuffer, RawCode);
        Uart_SendBlocking(TxBuffer);
        Oled_DrawTemperature(RawCode);

        /* Publish the reading so the web page serves the current value. */
        Http_SetTemperature(RawCode);

        /* Update relay outputs based on temperature vs thermostat setpoint. */
        Relay_UpdateControl(RawCode);
    }
    else
    {
        Uart_SendBlocking("I2C read failed\r\n");

        /* Make the failure visible on the panel as well as on the console. */
        SSD1306_FillRect(OLED_VALUE_X, OLED_ROW_TEMP, OLED_VALUE_W,
                         OLED_CHAR_HEIGHT, false);
        SSD1306_DrawString(OLED_VALUE_X, OLED_ROW_TEMP, "--.- C", 1, true);
    }

    /* Refresh the frame counter and push the whole buffer out in one pass, so
     * the temperature and the counter are flushed by a single I2c burst. */
    Oled_DrawRxCount();
    SSD1306_UpdateScreen();
}

/*==================================================================================================
 *   main
 ==================================================================================================*/


/**
 * @brief   Application entry point.
 * @return  0 (never reached in normal operation).
 */
int main(void) {
	Eth_RxStatusType rxStatus = ETH_NOT_RECEIVED;
	uint32 watchdog = GMAC_WATCHDOG_RELOAD;
	uint32 tempSampleCtr = TEMP_SAMPLE_PERIOD;
	sint16 tempSetpoint = (sint16)TEMP_SETPOINT_DEFAULT;

	/*==========================================================================
	 * Initialization
	 *=========================================================================*/

	/* Initialize the OS abstraction layer (tick counter, critical sections). */
	OsIf_Init(NULL_PTR);

	/* Initialize MCU driver (clock sources, RAM wait-states). */
	Mcu_Init(NULL_PTR);

#ifdef PHY_INIT_BY_SOFTWARE
	/* Issue a hardware reset to the PHY chip via the reset GPIO. */
    EthPhy_PhyHwReset();
#endif

	/* Configure all pin mux settings (RMII data lines, MDC/MDIO, LPI2C, LPUART, GPIOs). */
	Port_Init(NULL_PTR);

#ifdef PHY_INIT_BY_PIN_STRAPPING
	/* Drive the PHY strap pins to the desired configuration levels. */
    EthPhy_PhyPinStrapping();
#endif

	/* Start the PLL and switch the system to the target clock configuration. */
	Mcu_InitClock(McuClockSettingConfig_0);

#if (MCU_NO_PLL == STD_OFF)
	/* Wait until the PLL has achieved lock before distributing the clock. */
	while (MCU_PLL_LOCKED != Mcu_GetPllStatus()) {
	}
	Mcu_DistributePllClock();
#endif

	/* Switch the MCU to the run mode (enables peripheral clock gates). */
	Mcu_SetMode(McuModeSettingConf_0);

	/* Initialize the GMAC DMA rings, FIFO thresholds, and interrupt masks. */
	Eth_43_GMAC_Init(NULL_PTR);

#ifdef PHY_INIT_BY_SOFTWARE
	/* Configure the PHY registers: no loopback, RMII 100 Mbit/s, full duplex. */
    EthPhy_PhyInit(EthConf_EthCtrlConfig_EthCtrlConfig_0,
                   ETH_NO_LOOPBACK, ETH_RMII_100_MBPS,
                   ETH_FULL_DUPLEX, ETH_MASTER_MODE);
#endif

	/* Bring the GMAC controller into ACTIVE mode to start the Ethernet link. */
	Eth_43_GMAC_SetControllerMode(EthConf_EthCtrlConfig_EthCtrlConfig_0,
			ETH_MODE_ACTIVE);

	/* Initialize the Mcl driver. This enables the FlexIO module itself by
	 * setting FLEXIO0->CTRL[FLEXEN] via Flexio_Mcl_Ip_InitDevice(). It MUST run
	 * before I2c_Init(), because the FlexIO I2c channel init only programs the
	 * shifter/timer registers and assumes the module is already enabled. It
	 * must also run after Mcu_SetMode(), so the FlexIO clock gate is open. */
	Mcl_Init(&Mcl_Config);

	/* Initialize the I2c driver. A single I2c_Init() brings up BOTH configured
	 * hardware channels at once: channel 0 = LPI2C (P3T1750 temperature sensor)
	 * and channel 1 = FlexIO I2C (SSD1306 OLED display). Note that this only
	 * configures the channels - enabling the FlexIO block is the job of
	 * Mcl_Init() above. */
	I2c_Init(&I2c_Config);
	Uart_Init(&Uart_xConfig);

	/* Initialize the SSD1306 OLED display (uses the FlexIO I2c channel brought
	 * up by Mcl_Init + I2c_Init). */
	SSD1306_Init();

	/* Paint the static dashboard chrome and the placeholder values. The value
	 * fields are refreshed later by Temp_SampleAndReport(); the IP address is
	 * fixed, so it is drawn once here and never repainted. */
	SSD1306_ClearBuffer();
	Oled_DrawDashboard();
	SSD1306_DrawString(OLED_VALUE_X, OLED_ROW_TEMP, "--.- C", 1, true);
	Oled_DrawSetpoint(tempSetpoint);
	SSD1306_DrawString(OLED_VALUE_X + OLED_CHAR_ADVANCE, OLED_ROW_RX, "0", 1, true);
	SSD1306_UpdateScreen();

	Uart_SendBlocking("P3T1750DP temperature monitor started (Ethernet active)\r\n");

	//Test
	Dio_WriteChannel(DioConf_DioChannel_RL1, STD_HIGH);
	Dio_WriteChannel(DioConf_DioChannel_RL2, STD_HIGH);
	//Off
	Dio_WriteChannel(DioConf_DioChannel_RL1, STD_LOW);
	Dio_WriteChannel(DioConf_DioChannel_RL2, STD_LOW);

	/*==========================================================================
	 * Main loop
	 *=========================================================================*/
	while (TRUE) {
		/*
		 * Step 1 - Receive.
		 * Drains the RX DMA ring; fires EthIf_RxIndication for every pending frame.
		 */
		Eth_43_GMAC_Receive(EthConf_EthCtrlConfig_EthCtrlConfig_0, 0U,
				&rxStatus);

		/*
		 * Step 2 - TX confirmation.
		 * Recycles completed TX descriptors so the TX ring does not fill up.
		 */
		Eth_43_GMAC_TxConfirmation(EthConf_EthCtrlConfig_EthCtrlConfig_0);

		/*
		 * Step 3 - Periodic temperature sample over LPI2C + report over LPUART.
		 * Runs once every TEMP_SAMPLE_PERIOD loop iterations so it does not
		 * starve the Ethernet polling above.
		 */
		if (tempSampleCtr == 0UL) {
			tempSampleCtr = TEMP_SAMPLE_PERIOD;
			Temp_SampleAndReport();
		} else {
			tempSampleCtr--;
		}

		/*
		 * Step 4 - Thermostat command handler.
		 * Reads the command mailbox set by Http_Server.c when command1/2/3
		 * is received over HTTP, adjusts the setpoint, updates the OLED
		 * setpoint row, and clears the mailbox.
		 */
		if (g_AppPendingCommand != 0U)
		{
			uint8 cmd = g_AppPendingCommand;
			g_AppPendingCommand = 0U;

			if (cmd == 1U)
			{
				/* command1: increase setpoint by 1 degree. */
				if (tempSetpoint < (sint16)TEMP_SETPOINT_MAX)
				{
					tempSetpoint++;
				}
			}
			else if (cmd == 2U)
			{
				/* command2: decrease setpoint by 1 degree. */
				if (tempSetpoint > (sint16)TEMP_SETPOINT_MIN)
				{
					tempSetpoint--;
				}
			}
			else if (cmd == 3U)
			{
				/* command3: on/off led */
				if(g_GreenLed == FALSE){
					g_GreenLed = TRUE;
					Dio_WriteChannel(DioConf_DioChannel_GREEN, STD_HIGH);
				} else{
					g_GreenLed = FALSE;
					Dio_WriteChannel(DioConf_DioChannel_GREEN, STD_LOW);
				}
			}
			else
			{
				/* unknown command - ignore */
			}

			/* Sync the file-scope variable used by Relay_UpdateControl(). */
			g_TempSetpoint = tempSetpoint;

			Oled_DrawSetpoint(tempSetpoint);
			SSD1306_UpdateScreen();
		}

		/*
		 * Step 5 - Software GMAC watchdog.
		 * Cycles the GMAC DOWN then ACTIVE every ~30 s to recover any DMA
		 * descriptor ownership mismatch without dropping the Ethernet link.
		 */
		if (watchdog == 0UL) {
			watchdog = GMAC_WATCHDOG_RELOAD;
			Eth_43_GMAC_SetControllerMode(EthConf_EthCtrlConfig_EthCtrlConfig_0,
					ETH_MODE_DOWN);
			Eth_43_GMAC_SetControllerMode(EthConf_EthCtrlConfig_EthCtrlConfig_0,
					ETH_MODE_ACTIVE);
		} else {
			watchdog--;
		}
	}

	/* Not reached in normal operation. */
	Eth_43_GMAC_SetControllerMode(EthConf_EthCtrlConfig_EthCtrlConfig_0,
			ETH_MODE_DOWN);
	return 0;
}
