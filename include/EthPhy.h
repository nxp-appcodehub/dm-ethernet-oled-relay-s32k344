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

/*   @implements EthPhy.h_Artifact */
#ifndef ETHPHY_H_
#define ETHPHY_H_

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
#include "Dio.h"
#include "Port.h"
#include "Eth_43_GMAC.h"

/*==================================================================================================
 *                                          CONSTANTS
 ==================================================================================================*/

/*-------------------------------- TJA1100/TJA1101 PHY Registers ---------------------------------*/
#define ETH_PHY_BCR_REG					(0x00U)
#define ETH_PHY_BSR_REG					(0x01U)
#define ETH_PHY_PHYIDR1_REG				(0x02U)
#define ETH_PHY_PHYIDR2_REG				(0x03U)
#define ETH_PHY_EXTND_STAT_REG			(0x0FU)
#define ETH_PHY_PHYIDR3_REG				(0x10U)
#define ETH_PHY_EXTND_CTRL_REG			(0x11U)
#define ETH_PHY_CFG_REG1_REG			(0x12U)
#define ETH_PHY_CFG_REG2_REG			(0x13U)
#define ETH_PHY_SYMBOL_ERR_CTR_REG		(0x14U)
#define ETH_PHY_IRQ_SOURCE_REG			(0x15U)
#define ETH_PHY_IRQ_ENABLE_REG			(0x16U)
#define ETH_PHY_COMM_STAT_REG			(0x17U)
#define ETH_PHY_GENERAL_STAT_REG		(0x18U)
#define ETH_PHY_EXT_STAT_REG			(0x19U)
#define ETH_PHY_LINK_FAIL_CTR_REG		(0x1AU)
#define ETH_PHY_COMMON_CFG_REG			(0x1BU)
#define ETH_PHY_CFG_REG3_REG			(0x1CU)

/*-------------------------------- TJA1103/TJA1120 PHY Registers ---------------------------------*/
#define ETH_PHY_PMA_CTRL1_REG			(0x00U)
#define ETH_PHY_PCS_CTRL1_REG			(0x00U)
#define ETH_PHY_PMA_STAT1_REG			(0x01U)
#define ETH_PHY_PCS_STAT1_REG			(0x01U)
#define ETH_PHY_IDENTIFIER0_REG			(0x02U)
#define ETH_PHY_IDENTIFIER1_REG			(0x03U)
#define ETH_PHY_PMA_SPEED_ABILITY_REG	(0x04U)
#define ETH_PHY_PCS_SPEED_ABILITY_REG	(0x04U)
#define ETH_PHY_DEVICE_IDENTIFIER3_REG	(0x04U)
#define ETH_PHY_PMA_CTRL2_REG			(0x07U)
#define ETH_PHY_PMA_STAT2_REG       	(0x08U)
#define ETH_PHY_PCS_STAT2_REG       	(0x08U)
#define ETH_PHY_PMD_TX_DISABLE_REG      (0x09U)
#define ETH_PHY_PMD_RCV_DETECT_REG		(0x0AU)
#define ETH_PHY_PMA_EXTND_ABILITY_REG	(0x0BU)
#define ETH_PHY_PMA_XTD_ABILITY_REG		(0x12U)
#define ETH_PHY_DEVICE_CTRL				(0x40U)
#define ETH_PHY_DEVICE_STAT				(0x42U)
#define ETH_PHY_DEVICE_STAT_LATCHED		(0x44U)
#define ETH_PHY_DEVICE_PROPERTIES		(0x47U)
#define ETH_PHY_DEVICE_CFG				(0x48U)
#define ETH_PHY_DEVICE_CFG_EXTND		(0x49U)
#define ETH_PHY_PORT_IRQ_STAT			(0x70U)
#define ETH_PHY_PORT_IRQ_ENABLE			(0x72U)
#define ETH_PHY_PORT_IRQ_MSTAT			(0x74U)
#define ETH_PHY_BROADCAST				(0x100U)
#define ETH_PHY_PORT1_CFG				(0x101U)
#define ETH_PHY_WISE_CTRL				(0x180U)
#define ETH_PHY_WISE_STAT				(0x181U)
#define ETH_PHY_WISE_CFG				(0x182U)
#define ETH_PHY_WISE_PARAM				(0x184U)
#define ETH_PHY_SMI_STAT				(0x189U)
#define ETH_PHY_SMI_CFG					(0x18AU)
#define ETH_PHY_SMI_DATA_IO_CFG			(0x18CU)
#define ETH_PHY_AO_SYS_SUPPLY_STAT		(0x311U)
#define ETH_PHY_CORE_SUPPLY_STAT		(0x313U)
#define ETH_PHY_VDDIO_SUPPLY_STAT		(0x315U)
#define ETH_PHY_VREGD_SUPPLY_STAT		(0x318U)
#define ETH_PHY_VREGA_SUPPLY_STAT		(0x319U)
#define ETH_PHY_VDDA_TRX_SUPPLY_STAT	(0x31AU)
#define ETH_PHY_OSC_STAT				(0x31EU)
#define ETH_PHY_TEMP_STAT				(0x31FU)
#define ETH_PHY_PMA_CTRL_REG			(0x834U)
#define ETH_PHY_PMA_TEST_CTRL_REG		(0x836U)
#define ETH_PHY_PTP_IDENTIFIER_REG		(0x1100U)
#define ETH_PHY_PTP_CTRL_REG			(0x1101U)
#define ETH_PHY_PTP_CFG_REG				(0x1102U)
#define ETH_PHY_PTP_ABILITY_REG			(0x1103U)
#define ETH_PHY_PTP_CLK_PERIOD_REG		(0x1104U)
#define ETH_PHY_LTC_LOAD_CTRL_REG		(0x1105U)
#define ETH_PHY_LTC_WR_NSEC0_REG		(0x1106U)
#define ETH_PHY_LTC_WR_SEC0_REG			(0x1108U)
#define ETH_PHY_HW_LTC_LOCK_CTRL_REG	(0x1115U)
#define ETH_PHY_HW_EXT_PPS_NS0_REG		(0x1116U)
#define ETH_PHY_HW_LOCK_ER_LMT0_REG		(0x1118U)
#define ETH_PHY_LTC_LOOP_CTRL_REG		(0x111AU)
#define ETH_PHY_EXT_PPS_TS_DATA0_REG	(0x111BU)
#define ETH_PHY_EXT_PPS_TS_DATA1_REG	(0x111CU)
#define ETH_PHY_EXT_PPS_TS_DATA2_REG	(0x111DU)
#define ETH_PHY_EXT_PPS_TS_DATA3_REG	(0x111EU)
#define ETH_PHY_EXT_PPS_TS_DATA4_REG	(0x111FU)
#define ETH_PHY_EXT_PPS_TS_CTRL_REG		(0x1120U)
#define ETH_PHY_EXT_TRG_TS_DATA0_REG	(0x1121U)
#define ETH_PHY_EXT_TRG_TS_DATA1_REG	(0x1122U)
#define ETH_PHY_EXT_TRG_TS_DATA2_REG	(0x1123U)
#define ETH_PHY_EXT_TRG_TS_DATA3_REG	(0x1124U)
#define ETH_PHY_EXT_TRG_TS_DATA4_REG	(0x1125U)
#define ETH_PHY_EXT_TRG_TS_CTRL_REG		(0x1123U)
#define ETH_PHY_PTP_IRQ_SOURCE_REG		(0x1130U)
#define ETH_PHY_PTP_IRQ_ENABLE_REG		(0x1131U)
#define ETH_PHY_PTP_IRQ_MSTAT_REG		(0x1132U)
#define ETH_PHY_PKT_FILT_CTRL_REG		(0x1140U)
#define ETH_PHY_DA_FILT_CTRL_REG		(0x1141U)
#define ETH_PHY_USER_MAC_DA0_REG		(0x1142U)
#define ETH_PHY_USER_MAC_MASK0_REG		(0x1145U)
#define ETH_PHY_XMII_CTRL_REG			(0xAFC0U)
#define ETH_PHY_XMII_STAT_REG			(0xAFC2U)
#define ETH_PHY_XMII_ABILITY_REG		(0xAFC4U)
#define ETH_PHY_SERDES_MII_ABILITY_REG	(0xAFC5U)
#define ETH_PHY_MII_BASIC_CFG_REG		(0xAFC6U)
#define ETH_PHY_XMII_CLK_CFG_REG		(0xAFC8U)
#define ETH_PHY_XMII_CLK_IO_CFG_REG		(0xAFC9U)
#define ETH_PHY_XMII_DATA_CFG_REG		(0xAFCAU)
#define ETH_PHY_XMII_DATA_IO_CFG_REG	(0xAFCBU)
#define ETH_PHY_RGMII_TXC_DELAY_CFG_REG	(0xAFCCU)
#define ETH_PHY_RGMII_RXC_DELAY_CFG_REG	(0xAFCDU)

/*---------------------------------------- DP83848 PHY Registers ---------------------------------*/
#define ETH_PHY_BMCR_REG				(0x00U)
#define ETH_PHY_BMSR_REG				(0x01U)
#define ETH_PHY_PHYIDR1_REG				(0x02U)
#define ETH_PHY_PHYIDR2_REG				(0x03U)
#define ETH_PHY_ANAR_REG				(0x04U)
#define ETH_PHY_ANLPAR_REG				(0x05U)
#define ETH_PHY_ANLPARNP_REG			(0x05U)
#define ETH_PHY_ANER_REG				(0x06U)
#define ETH_PHY_ANNPTR_REG				(0x07U)
#define ETH_PHY_ANLNPTR_REG       		(0x08U)
#define ETH_PHY_SWSCR1_REG				(0x09U)
#define ETH_PHY_SWSCR2_REG				(0x0AU)
#define ETH_PHY_SWSCR3_REG				(0x0BU)
#define ETH_PHY_REGCR_REG				(0x0DU)
#define ETH_PHY_ADDAR_REG				(0x0EU)
#define ETH_PHY_PHYSTS_REG				(0x10U)
#define ETH_PHY_MICR_REG				(0x11U)
#define ETH_PHY_PHYSCR_REG				(0x11U)
#define ETH_PHY_MISR_REG				(0x12U)
#define ETH_PHY_MISR1_REG				(0x12U)
#define ETH_PHY_MISR2_REG				(0x13U)
#define ETH_PHY_FCSCR_REG				(0x14U)
#define ETH_PHY_RECR_REG				(0x15U)
#define ETH_PHY_PCSR_REG				(0x16U)
#define ETH_PHY_BISCR_REG				(0x16U)
#define ETH_PHY_RBR_REG					(0x17U)
#define ETH_PHY_LEDCR_REG				(0x18U)
#define ETH_PHY_PHYCR_REG				(0x19U)
#define ETH_PHY_10BTSCR_REG				(0x1AU)
#define ETH_PHY_CDCTRL1_REG				(0x1BU)
#define ETH_PHY_BICSR1_REG				(0x1BU)
#define ETH_PHY_BICSR2_REG				(0x1CU)
#define ETH_PHY_EDCR_REG				(0x1DU)
#define ETH_PHY_CDCR_REG				(0x1EU)
#define ETH_PHY_PHYRCR_REG				(0x1FU)

/*==================================================================================================
 *                                      DEFINES AND MACROS
 ==================================================================================================*/

/*==================================================================================================
 *                                            ENUMS
 ==================================================================================================*/
typedef enum {
	ETH_NO_LOOPBACK, ETH_LOOPBACK
} EthPhy_TestConfiguration;

typedef enum {
	ETH_MII_10_MBPS,
	ETH_RMII_10_MBPS,
	ETH_REV_RMII_10_MBPS,
	ETH_MII_100_MBPS,
	ETH_RMII_100_MBPS,
	ETH_REV_RMII_100_MBPS,
	ETH_MII_200_MBPS,
	ETH_RGMII_10_MBPS,
	ETH_RGMII_100_MBPS,
	ETH_RGMII_1000_MBPS
} EthPhy_TestMode;

typedef enum {
	ETH_HALF_DUPLEX, ETH_FULL_DUPLEX
} EthPhy_Duplex;

typedef enum {
	TJA1100_PHY, TJA1101_PHY, TJA1103_PHY, TJA1120_PHY, DP83848_PHY
} EthPhy_PhyIc;

typedef enum {
	ETH_MASTER_MODE, ETH_SALVE_MODE
} EthPhy_MasterMode;

/*==================================================================================================
 *                                STRUCTURES AND OTHER TYPEDEFS
 ==================================================================================================*/

/*==================================================================================================
 *                                GLOBAL VARIABLE DECLARATIONS
 ==================================================================================================*/

/*==================================================================================================
 *                                    FUNCTION PROTOTYPES
 ==================================================================================================*/
#if STD_ON == ETH_43_GMAC_MDIO_CLAUSE22_API

/**
 * @brief         Returns a the link status of the Ethernet PHY.
 * @note          IEEE Clause 22 or IEEE Clause 45 should be enabled to access the PHY registers.
 *
 * @api
 * @param[in]     CtrlIdx Index of the Ethernet controller.
 * @param[in]     EthPhyAddr Ethernet PHY address used by the SMI interface.
 * @param[in]     EthPhyType Ethernet PHY vendor or device.
 * @return        Error status
 * @retval        E_OK: Ethernet link is up.
 * @retval        E_NOT_OK: Ethernet link is down.
 * implements     EthPhy_IsPhyLinkUp
 */
Std_ReturnType EthPhy_IsPhyLinkUp(uint8 CtrlIdx, uint8 EthPhyAddr,
		EthPhy_PhyIc EthPhyType);

/**
 * @brief         Configures the selected Ethernet PHY according to the user parameters.
 * @note          IEEE Clause 22 or IEEE Clause 45 should be enabled to access the PHY registers.
 *
 * @api
 * @param[in]     CtrlIdx Index of the Ethernet controller.
 * @param[in]     EthPhyAddr Ethernet PHY address used by the SMI interface.
 * @param[in]     EthPhyType Ethernet PHY vendor or device.
 * @param[in]     EthPhyConfig Ethernet PHY loopback mode selection.
 * @param[in]     EthPhyMode Ethernet PHY operational mode selection.
 * @param[in]     EthPhyDuplex Ethernet PHY duplex mode selection.
 * @param[in]     EthMasterMode Ethernet PHY master or slave selection.
 * implements     EthPhy_PhyConfig
 */
void EthPhy_PhyConfig(uint8 CtrlIdx, uint8 EthPhyAddr, EthPhy_PhyIc EthPhyType,
		EthPhy_TestConfiguration EthPhyConfig, EthPhy_TestMode EthPhyMode,
		EthPhy_Duplex EthPhyDuplex, EthPhy_MasterMode EthMasterMode);

/**
 * @brief         Resets the Ethernet PHY by software.
 * @note          IEEE Clause 22 or IEEE Clause 45 should be enabled to access the PHY registers.
 *
 * @api
 * @param[in]     CtrlIdx Index of the Ethernet controller.
 * @param[in]     EthPhyAddr Ethernet PHY address used by the SMI interface.
 * @param[in]     EthPhyType Ethernet PHY vendor or device.
 * @return        Error status
 * @retval        E_OK: Ethernet PHY is successfully out of reset.
 * @retval        E_NOT_OK: Ethernet PHY is not out of reset.
 * implements     EthPhy_PhySwReset
 */
Std_ReturnType EthPhy_PhySwReset(uint8 CtrlIdx, uint8 EthPhyAddr,
		EthPhy_PhyIc EthPhyType);

/**
 * @brief         Resets the Ethernet PHY by hardware.
 *
 * @api
 * implements     EthPhy_PhyHwReset
 */
void EthPhy_PhyHwReset(void);

/**
 * @brief         Initializes the Ethernet PHY via pin strapping.
 *
 * @api
 * implements     EthPhy_PhyPinStrapping
 */
void EthPhy_PhyPinStrapping(void);

/**
 * @brief         Initializes the Ethernet PHY according to the user parameters.
 * @note          IEEE Clause 22 or IEEE Clause 45 should be enabled to access the PHY registers.
 *
 * @api
 * @param[in]     CtrlIdx Index of the Ethernet controller.
 * @param[in]     EthPhyConfig Ethernet PHY loopback mode selection.
 * @param[in]     EthPhyMode Ethernet PHY operational mode selection.
 * @param[in]     EthPhyDuplex Ethernet PHY duplex mode selection.
 * @param[in]     EthMasterMode Ethernet PHY master or slave selection.
 * @return        Error status
 * @retval        E_OK: Ethernet PHY is successfully initialized.
 * @retval        E_NOT_OK: Ethernet PHY is not successfully initialized.
 * implements     EthPhy_PhyInit
 */
Std_ReturnType EthPhy_PhyInit(uint8 CtrlIdx,
		EthPhy_TestConfiguration EthPhyConfig, EthPhy_TestMode EthPhyMode,
		EthPhy_Duplex EthPhyDuplex, EthPhy_MasterMode EthMasterMode);

#endif /* ETH_43_GMAC_MDIO_CLAUSE22_API */

/** @} */

#endif /* ETHPHY_H_ */

