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
 *   @file
 *
 *   @addtogroup ETHPHY Ethernet Driver
 *   @{
 */

#ifdef __cplusplus
extern "C"{
#endif

/*==================================================================================================
 *                                        INCLUDE FILES
 * 1) system and project includes
 * 2) needed interfaces from external units
 * 3) internal and external interfaces from this unit
 ==================================================================================================*/
#include "EthPhy.h"

/*==================================================================================================
 *                          LOCAL TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
 ==================================================================================================*/

/*==================================================================================================
 *                                       LOCAL MACROS
 ==================================================================================================*/
#define ETH_PHY_BMSR_LINK_STATUS	(0x0004U)
#define ETH_PHY_BMSR_RESET_STATUS	(0x8000U)

/* Ethernet PHY MMD values for IEEE Clause 45 */
#define ETH_PHY_MMD01				(01U)
#define ETH_PHY_MMD03				(03U)
#define ETH_PHY_MMD30				(30U)

/*==================================================================================================
 *                                      LOCAL CONSTANTS
 ==================================================================================================*/

/* Ethernet PHY IDs */
#define TJA1100_PHY_ID0    (0x0180U)
#define TJA1100_PHY_ID1    (0xDC41U)

#define TJA1101_PHY_ID0    (0x0180U)
#define TJA1101_PHY_ID1    (0xDD01U)

#define TJA1103_PHY_ID0    (0x001BU)
#define TJA1103_PHY_ID1    (0xB013U)

#define TJA1120_PHY_ID0    (0x001BU)
#define TJA1120_PHY_ID1    (0x2C30U)

#define DP83848_PHY_ID0    (0x2000U)
#define DP83848_PHY_ID1    (0x5C90U)

/*==================================================================================================
 *                                      LOCAL VARIABLES
 ==================================================================================================*/

/*==================================================================================================
 *                                       LOCAL FUNCTIONS
 ==================================================================================================*/

/*==================================================================================================
 *                                       GLOBAL FUNCTIONS
 ==================================================================================================*/
#if STD_ON == ETH_43_GMAC_MDIO_CLAUSE22_API

/*================================================================================================*/
Std_ReturnType EthPhy_IsPhyLinkUp(uint8 CtrlIdx, uint8 EthPhyAddr,
		EthPhy_PhyIc EthPhyType) {
	uint16 RegValRead = 0U;

	/* Read Status Register with IEEE Clause 22 */
	if ((TJA1100_PHY == EthPhyType) || (TJA1101_PHY == EthPhyType)
			|| (DP83848_PHY == EthPhyType)) {
		/* Read ETH PHY Status register */
		Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_BMSR_REG, &RegValRead);

		if (RegValRead & ETH_PHY_BMSR_LINK_STATUS) {
			return E_OK; /* Link is up */
		}

		else {
			return E_NOT_OK; /* Link is down */
		}
	}

	/* Read Status register with IEEE Clause 45 */
	else if ((TJA1103_PHY == EthPhyType) || (TJA1120_PHY == EthPhyType)) {
		/* Read ETH PHY Status register */
		Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD01,
		ETH_PHY_BMSR_REG, &RegValRead);

		if (RegValRead & ETH_PHY_BMSR_LINK_STATUS) {
			return E_OK; /* Link is up */
		}

		else {
			return E_NOT_OK; /* Link is down */
		}
	}

	else {
		return E_NOT_OK; /* Link is down */
	}
}

/*================================================================================================*/
void EthPhy_PhyConfig(uint8 CtrlIdx, uint8 EthPhyAddr, EthPhy_PhyIc EthPhyType,
		EthPhy_TestConfiguration EthPhyConfig, EthPhy_TestMode EthPhyMode,
		EthPhy_Duplex EthPhyDuplex, EthPhy_MasterMode EthMasterMode) {
	uint16 Duplex = 0U;
	uint16 Loopback = 0U;
	uint16 RevModeSel = 0U;
	uint16 MasterMode = 0U;
	uint16 ModeSelMsb = 0U;
	uint16 ModeSelLsb = 0U;
	uint16 SpeedSelMsb = 0U;
	uint16 SpeedSelLsb = 0U;
	uint16 AutoNegotiationRst = 0U;
	uint16 AutoNegotiationEnable = 0U;

	uint16 RegVal = 0U;
	uint16 RegValRead = 0U;

	/* Check loopback mode */
	if (ETH_LOOPBACK == EthPhyConfig) {
		Loopback = 1U;
	}

	/* Check master mode */
	if (ETH_MASTER_MODE == EthMasterMode) {
		MasterMode = 1U;
	}

	/* Check operating speed of 10 Mbps */
	if ((ETH_MII_10_MBPS == EthPhyMode) || (ETH_RMII_10_MBPS == EthPhyMode)
			|| (ETH_REV_RMII_10_MBPS == EthPhyMode)
			|| (ETH_RGMII_10_MBPS == EthPhyMode)) {
		SpeedSelMsb = 0U;
		SpeedSelLsb = 0U;
	}

	/* Check operating speed of 100 Mbps */
	else if ((ETH_MII_100_MBPS == EthPhyMode)
			|| (ETH_RMII_100_MBPS == EthPhyMode)
			|| (ETH_REV_RMII_100_MBPS == EthPhyMode)
			|| (ETH_RGMII_100_MBPS == EthPhyMode)) {
		SpeedSelMsb = 0U;
		SpeedSelLsb = 1U;
	}

	/* Check operating speed of 1000 Mbps */
	else if (ETH_RGMII_1000_MBPS == EthPhyMode) {
		SpeedSelMsb = 1U;
		SpeedSelLsb = 0U;
	}

	/* Check operating mode of MII */
	if ((ETH_MII_10_MBPS == EthPhyMode) || (ETH_MII_100_MBPS == EthPhyMode)) {
		ModeSelMsb = 0U;
		ModeSelLsb = 0U;
	}

	/* Check operating mode of RMII */
	else if ((ETH_RMII_10_MBPS == EthPhyMode)
			|| (ETH_RMII_100_MBPS == EthPhyMode)) {
		ModeSelMsb = 0U;
		ModeSelLsb = 1U;
	}

	/* Check operating mode of RGMII */
	else if ((ETH_RGMII_10_MBPS == EthPhyMode)
			|| (ETH_RGMII_100_MBPS == EthPhyMode)
			|| (ETH_RGMII_1000_MBPS == EthPhyMode)) {
		ModeSelMsb = 1U;
		ModeSelLsb = 1U;
	}

	/* Check reverse mode */
	if ((ETH_REV_RMII_10_MBPS == EthPhyMode)
			|| (ETH_REV_RMII_100_MBPS == EthPhyMode)) {
		RevModeSel = 1U;
	}

	/* Check duplex mode */
	if (ETH_FULL_DUPLEX == EthPhyDuplex) {
		Duplex = 1U;
	}

	/* Configure TJA1100/TJA1101 PHY according to the previous settings using IEEE Clause 22 */
	if ((TJA1100_PHY == EthPhyType) || (TJA1101_PHY == EthPhyType)) {
		/* TJA1100/TJA1101 PHY only supports MII/RMII/RevRMII at 10/100 Mbps */
		if ((ETH_MII_10_MBPS == EthPhyMode) || (ETH_RMII_10_MBPS == EthPhyMode)
				|| (ETH_REV_RMII_10_MBPS == EthPhyMode)
				|| (ETH_MII_100_MBPS == EthPhyMode)
				|| (ETH_RMII_100_MBPS == EthPhyMode)
				|| (ETH_REV_RMII_100_MBPS == EthPhyMode)) {
			/* Configure ETH PHY Control register */
			RegVal = (Loopback << 14U) | (SpeedSelLsb << 13U)
					| (AutoNegotiationEnable << 12U)
					| (AutoNegotiationRst << 9U) | (Duplex << 8U)
					| (SpeedSelMsb << 6U);
			Eth_43_GMAC_WriteMii(CtrlIdx, EthPhyAddr, ETH_PHY_BCR_REG, RegVal);
			Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_BCR_REG,
					&RegValRead);

			/* Configure ETH PHY Extended Control register */
			Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_EXTND_CTRL_REG,
					&RegValRead);
			RegVal = RegValRead | (1U << 2U);
			Eth_43_GMAC_WriteMii(CtrlIdx, EthPhyAddr, ETH_PHY_EXTND_CTRL_REG,
					RegVal);
			Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_EXTND_CTRL_REG,
					&RegValRead);

			/* Configure ETH PHY Extended Control register */
			Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_CFG_REG1_REG,
					&RegValRead);
			RegVal = RegValRead | (MasterMode << 15U) | (RevModeSel << 9U)
					| (ModeSelLsb << 8U);
			Eth_43_GMAC_WriteMii(CtrlIdx, EthPhyAddr, ETH_PHY_CFG_REG1_REG,
					RegVal);
			Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_CFG_REG1_REG,
					&RegValRead);
		}
	}

	/* Configure TJA1103 PHY according to the previous settings using IEEE Clause 45 */
	else if (TJA1103_PHY == EthPhyType) {
		/* TJA1103 PHY only supports MII/RMII/RevRMII/RGMII at 100 Mbps */
		if ((ETH_MII_100_MBPS == EthPhyMode)
				|| (ETH_RMII_100_MBPS == EthPhyMode)
				|| (ETH_REV_RMII_100_MBPS == EthPhyMode)
				|| (ETH_RGMII_100_MBPS == EthPhyMode)) {
			/* Configure ETH PHY Device Control register */
			RegVal = (1U << 14U) | (1U << 13U);
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_DEVICE_CTRL, RegVal);
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_DEVICE_CTRL, &RegValRead);

			/* Configure ETH PHY Basic Configuration register */
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_MII_BASIC_CFG_REG, &RegValRead);
			RegVal = RegValRead | (RevModeSel << 4U) | (1U << 2U)
					| (ModeSelMsb << 1U) | (ModeSelLsb << 0U);
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_MII_BASIC_CFG_REG, RegVal);
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_MII_BASIC_CFG_REG, &RegValRead);

			/* Configure ETH PHY RGMII TXC Delay Configuration register */
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_RGMII_TXC_DELAY_CFG_REG, &RegValRead);
			RegVal = RegValRead | (RevModeSel << 15U);
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_RGMII_TXC_DELAY_CFG_REG, RegVal);
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_RGMII_TXC_DELAY_CFG_REG, &RegValRead);

			/* Configure ETH PHY RGMII RXC Delay Configuration register */
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_RGMII_RXC_DELAY_CFG_REG, &RegValRead);
			RegVal = RegValRead | (RevModeSel << 15U);
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_RGMII_RXC_DELAY_CFG_REG, RegVal);
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_RGMII_RXC_DELAY_CFG_REG, &RegValRead);

			/* Configure ETH PHY PCS Control register */
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD03,
			ETH_PHY_PCS_CTRL1_REG, &RegValRead);
			RegVal = RegValRead | (Loopback << 14U);
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD03,
			ETH_PHY_PCS_CTRL1_REG, RegVal);
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD03,
			ETH_PHY_PCS_CTRL1_REG, &RegValRead);

			/* Configure ETH PHY PMA Control register */
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD01,
			ETH_PHY_PMA_CTRL_REG, &RegValRead);
			RegVal = RegValRead | (MasterMode << 14U);
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD01,
			ETH_PHY_PMA_CTRL_REG, RegVal);
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD01,
			ETH_PHY_PMA_CTRL_REG, &RegValRead);

			/* Enable ETH PHY Device Operation */
			RegVal = 1U;
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_DEVICE_CTRL, RegVal);
		}
	}

	/* Configure TJA1120 PHY according to the previous settings using IEEE Clause 45 */
	else if (TJA1120_PHY == EthPhyType) {
		/* TJA1120 PHY only supports RGMII at 1000 Mbps */
		if (ETH_RGMII_1000_MBPS == EthPhyMode) {
			/* Configure ETH PHY Device Control register */
			RegVal = (1U << 14U) | (1U << 13U);
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_DEVICE_CTRL, RegVal);
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_DEVICE_CTRL, &RegValRead);

			/* Configure ETH PHY RGMII TXC Delay Configuration register */
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_RGMII_TXC_DELAY_CFG_REG, &RegValRead);
			RegVal = RegValRead | (RevModeSel << 15U);
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_RGMII_TXC_DELAY_CFG_REG, RegVal);
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_RGMII_TXC_DELAY_CFG_REG, &RegValRead);

			/* Configure ETH PHY RGMII RXC Delay Configuration register */
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_RGMII_RXC_DELAY_CFG_REG, &RegValRead);
			RegVal = RegValRead | (RevModeSel << 15U);
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_RGMII_RXC_DELAY_CFG_REG, RegVal);
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_RGMII_RXC_DELAY_CFG_REG, &RegValRead);

			/* Configure ETH PHY PCS Control register */
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD03,
			ETH_PHY_PCS_CTRL1_REG, &RegValRead);
			RegVal = RegValRead | (Loopback << 14U);
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD03,
			ETH_PHY_PCS_CTRL1_REG, RegVal);
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD03,
			ETH_PHY_PCS_CTRL1_REG, &RegValRead);

			/* Configure ETH PHY PMA Control register */
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD01,
			ETH_PHY_PMA_CTRL_REG, &RegValRead);
			RegVal = RegValRead | (MasterMode << 14U);
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD01,
			ETH_PHY_PMA_CTRL_REG, RegVal);
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD01,
			ETH_PHY_PMA_CTRL_REG, &RegValRead);

			/* Enable ETH PHY Device Operation */
			RegVal = 1U;
			Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD30,
			ETH_PHY_DEVICE_CTRL, RegVal);
		}
	}

	/* Configure DP83848 PHY according to the previous settings using IEEE Clause 22 */
	else if ((DP83848_PHY == EthPhyType)) {
		/* DP83848 PHY only supports MII/RMII at 10/100 Mbps */
		if ((ETH_MII_10_MBPS == EthPhyMode) || (ETH_RMII_10_MBPS == EthPhyMode)
				|| (ETH_MII_100_MBPS == EthPhyMode)
				|| (ETH_RMII_100_MBPS == EthPhyMode)) {
			/* Configure ETH PHY Control register */
			RegVal = (Loopback << 14U) | (SpeedSelLsb << 13U)
					| (AutoNegotiationEnable << 12U)
					| (AutoNegotiationRst << 9U) | (Duplex << 8U);
			Eth_43_GMAC_WriteMii(CtrlIdx, EthPhyAddr, ETH_PHY_BMCR_REG, RegVal);
			Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_BMCR_REG,
					&RegValRead);

			/* Configure ETH PHY Operating Mode register */
			Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_RBR_REG,
					&RegValRead);
			RegVal = RegValRead | (ModeSelLsb << 5U);
			Eth_43_GMAC_WriteMii(CtrlIdx, EthPhyAddr, ETH_PHY_RBR_REG, RegVal);
			Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_RBR_REG,
					&RegValRead);
		}
	}
}

/*================================================================================================*/
Std_ReturnType EthPhy_PhySwReset(uint8 CtrlIdx, uint8 EthPhyAddr,
		EthPhy_PhyIc EthPhyType) {
	uint16 RegVal = 0U;
	uint16 RegValRead = 0U;

	/* ETH PHY reset with IEEE Clause 22 */
	if ((TJA1100_PHY == EthPhyType) || (TJA1101_PHY == EthPhyType)
			|| (DP83848_PHY == EthPhyType)) {
		/* Assert ETH PHY software reset bit */
		Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_BMCR_REG, &RegValRead);
		RegVal = RegValRead | (1U << 15U);
		Eth_43_GMAC_WriteMii(CtrlIdx, EthPhyAddr, ETH_PHY_BMCR_REG, RegVal);

		/* Wait until ETH PHY is out of reset */
		do {
			Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_BMCR_REG,
					&RegValRead);
			RegVal = RegValRead & ETH_PHY_BMSR_RESET_STATUS;

		} while (RegVal);
	}

	/* ETH PHY reset with IEEE Clause 45 */
	else if ((TJA1103_PHY == EthPhyType) || (TJA1120_PHY == EthPhyType)) {
		/* Assert ETH PHY software reset bit */
		Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD01,
		ETH_PHY_BMCR_REG, &RegValRead);
		RegVal = RegValRead | (1U << 15U);
		Eth_43_GMAC_WriteMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD01,
		ETH_PHY_BMCR_REG, RegVal);

		/* Wait until ETH PHY is out of reset */
		do {
			Eth_43_GMAC_ReadMmd(CtrlIdx, EthPhyAddr, ETH_PHY_MMD01,
			ETH_PHY_BMCR_REG, &RegValRead);
			RegVal = RegValRead & ETH_PHY_BMSR_RESET_STATUS;

		} while (RegVal);
	}

	return E_OK; /* ETH PHY is out of reset */
}

/*================================================================================================*/
void EthPhy_PhyHwReset(void) {
	/* Assert ETH PHY hardware reset */
	Dio_WriteChannel(DioConf_DioChannel_ETH_PHY_RST_PIN, STD_LOW);

	/* Dummy cycle to wait for a proper reset */
	for (uint32 Index = 0U; Index < 100U; Index++) {
		__asm("nop");
	}

	/* De-assert ETH PHY hardware reset */
	Dio_WriteChannel(DioConf_DioChannel_ETH_PHY_RST_PIN, STD_HIGH);

	/* Dummy cycle to wait for a proper enablement */
	for (uint32 Index = 0U; Index < 1000U; Index++) {
		__asm("nop");
	}
}

/*================================================================================================*/
void EthPhy_PhyPinStrapping(void) {
	/* Assert ETH PHY hardware reset */
	Dio_WriteChannel(DioConf_DioChannel_ETH_PHY_RST_PIN, STD_LOW);

	/* Dummy cycle to wait for a proper reset */
	for (uint32 Index = 0U; Index < 100U; Index++) {
		__asm("nop");
	}

	/* De-assert ETH PHY hardware reset */
	Dio_WriteChannel(DioConf_DioChannel_ETH_PHY_RST_PIN, STD_HIGH);

	/* Dummy cycle to wait for a proper enablement */
	for (uint32 Index = 0U; Index < 1000U; Index++) {
		__asm("nop");
	}
}

/*================================================================================================*/
Std_ReturnType EthPhy_PhyInit(uint8 CtrlIdx,
		EthPhy_TestConfiguration EthPhyConfig, EthPhy_TestMode EthPhyMode,
		EthPhy_Duplex EthPhyDuplex, EthPhy_MasterMode EthMasterMode) {
	uint16 RegValRead0 = 0U;
	uint16 RegValRead1 = 0U;
	EthPhy_PhyIc EthPhyType = 0U;
	static uint16 EthPhyAddr = 0U;

	/* Search for the ETH PHY address */
	for (EthPhyAddr = 0U; EthPhyAddr < 32U; EthPhyAddr++) {
		/* Get the ETH PHY ID to identify the address */
		Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_PHYIDR1_REG,
				&RegValRead0);
		Eth_43_GMAC_ReadMii(CtrlIdx, EthPhyAddr, ETH_PHY_PHYIDR2_REG,
				&RegValRead1);

		if (((TJA1100_PHY_ID0 == RegValRead0)
				&& (TJA1100_PHY_ID1 == RegValRead1))
				|| ((TJA1101_PHY_ID0 == RegValRead0)
						&& (TJA1101_PHY_ID1 == RegValRead1))
				|| ((TJA1103_PHY_ID0 == RegValRead0)
						&& (TJA1103_PHY_ID1 == RegValRead1))
				|| ((TJA1120_PHY_ID0 == RegValRead0)
						&& (TJA1120_PHY_ID1 == RegValRead1))
				|| ((DP83848_PHY_ID0 == RegValRead0)
						&& (DP83848_PHY_ID1 == RegValRead1))) {
			break; /* ETH PHY Address found */
		}
	}

	if (32U == EthPhyAddr) {
		/* ETH PHY Address not found */
		return E_NOT_OK;
	}

	/* Assign the ETH PHY */
	if (TJA1100_PHY_ID1 == RegValRead1) {
		EthPhyType = TJA1100_PHY;
	}

	else if (TJA1101_PHY_ID1 == RegValRead1) {
		EthPhyType = TJA1101_PHY;
	}

	else if (TJA1103_PHY_ID1 == RegValRead1) {
		EthPhyType = TJA1103_PHY;
	}

	else if (TJA1120_PHY_ID1 == RegValRead1) {
		EthPhyType = TJA1120_PHY;
	}

	else if (DP83848_PHY_ID1 == RegValRead1) {
		EthPhyType = DP83848_PHY;
	}

	/* Reset ETH PHY */
	if (E_OK == EthPhy_PhySwReset(CtrlIdx, EthPhyAddr, EthPhyType)) {
		/* Configure ETH PHY */
		EthPhy_PhyConfig(CtrlIdx, EthPhyAddr, EthPhyType, EthPhyConfig,
				EthPhyMode, EthPhyDuplex, EthMasterMode);

		/* Wait until ETH PHY link is up */
		while (E_OK != EthPhy_IsPhyLinkUp(CtrlIdx, EthPhyAddr, EthPhyType))
			;

		return E_OK; /* ETH PHY configuration successful */
	}

	else {
		return E_NOT_OK; /* ETH PHY configuration non successful */
	}
}

#endif /* ETH_43_GMAC_MDIO_CLAUSE22_API */

#ifdef __cplusplus
}
#endif

/** @} */
