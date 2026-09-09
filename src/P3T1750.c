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

/*==================================================================================================
 *                                        INCLUDE FILES
 ==================================================================================================*/
#include "P3T1750.h"   /* Uses the AUTOSAR MCAL I2c API (CDD_I2c.h) */

/*==================================================================================================
 *                                       GLOBAL FUNCTIONS
 ==================================================================================================*/

/**
 * @brief Read 2 bytes of raw temperature data from P3T1750 sensor
 *
 * Reads from register 0x00 (temperature data register).
 * Uses I2C repeated start for register address + data read.
 *
 * The read is performed with the AUTOSAR MCAL I2c API:
 *   1) I2c_AsyncTransmit() writes the register pointer (repeated start kept).
 *   2) I2c_AsyncTransmit() reads the 2 temperature bytes.
 * I2c_GetStatus() is polled between transfers.
 *
 * @param tempData  Pointer to buffer (minimum 2 bytes)
 * @return I2C_CH_IDLE on success, error status otherwise
 */
I2c_StatusType ReadTemperature(uint8 *tempData) {
	I2c_StatusType status;
	uint32 timeout;
	static uint8 regAddr = 0x00U; /* Temperature data register */

	/* Transfer descriptor for writing the register pointer (repeated start). */
	I2c_RequestType writeReq = {
	TEMP_ADDR, /* Slave address (7-bit) */
	FALSE, /* 10-bit address */
	FALSE, /* expect Nack */
	TRUE, /* repeated start (keep the bus for the read) */
	1U, /* buffer size */
	I2C_SEND_DATA, /* data direction */
	&regAddr /* buffer */
	};

	/* Transfer descriptor for reading the 2 temperature bytes. */
	I2c_RequestType readReq = {
	TEMP_ADDR, /* Slave address (7-bit) */
	FALSE, /* 10-bit address */
	FALSE, /* expect Nack */
	FALSE, /* repeated start */
	2U, /* buffer size */
	I2C_RECEIVE_DATA, /* data direction */
	tempData /* buffer */
	};

	/* Step 1: send the register pointer. */
	(void) I2c_AsyncTransmit(I2C_MASTER_CHANNEL, &writeReq);

	timeout = TIMEOUT;
	while (((status = I2c_GetStatus(I2C_MASTER_CHANNEL)) == I2C_CH_SEND)
			&& (timeout > 0U)) {
		timeout--;
	}

	if (status == I2C_CH_IDLE) {
		/* Step 2: read the temperature data. */
		(void) I2c_AsyncTransmit(I2C_MASTER_CHANNEL, &readReq);

		timeout = TIMEOUT;
		while (((status = I2c_GetStatus(I2C_MASTER_CHANNEL)) == I2C_CH_RECEIVE)
				&& (timeout > 0U)) {
			timeout--;
		}
	}

	return status;
}

/**
 * @brief Convert raw P3T1750 data to temperature in degrees Celsius
 *
 * The sensor provides a 12-bit value in bits [15:4] of the 16-bit register.
 * Resolution: 1 LSB = 0.0625 degrees C. Supports negative temperatures via
 * 2's complement sign extension.
 *
 * @param tempData  Raw data buffer (2 bytes: MSB, LSB)
 * @return Temperature in degrees Celsius
 */
float ConvertTemperature_TMP102(uint8 *tempData) {
	sint16 rawTemp;
	float temperature;

	/* Combine MSB and LSB into 16-bit value */
	rawTemp = (sint16) (((uint16) tempData[0] << 8) | (uint16) tempData[1]);

	/* Right-shift by 4 to extract 12-bit temperature value */
	rawTemp = (sint16) (rawTemp >> 4);

	/* Sign-extend for negative temperatures (bit 11 set) */
	if ((rawTemp & 0x800) != 0) {
		rawTemp |= (sint16) 0xF000;
	}

	/* Convert to Celsius: 1 LSB = 0.0625 degrees C */
	temperature = (float) rawTemp * 0.0625f;

	return temperature - MCU_TEMP_OFFSET;
}
