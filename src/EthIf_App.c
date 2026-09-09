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
*   Application-owned EthIf callback implementations.
*
*   Background:
*   The AUTOSAR RTD code generator emits a placeholder RTD/src/EthIf.c that
*   contains stub implementations of the EthIf upper-layer callbacks (the
*   functions the Eth_43_GMAC driver calls by fixed AUTOSAR names). Because that
*   file is regenerated on every "Update Code and Build Project" run, it must
*   not be hand-edited and it is not tracked by this repository.
*
*   This file is the single application-owned replacement for the generated
*   RTD/src/EthIf.c. The generated file is excluded from the build through the
*   project settings (.cproject, source entry for the RTD folder), and every
*   callback/global it used to provide is supplied here instead:
*     - global counters and state arrays (EthIf_RxIndications, ...)
*     - EthIf_RxIndication: the real receive path of this example, dispatching
*       each frame to Eth_HandleFrame and releasing the RX buffer afterwards
*     - EthIf_TxConfirmation, EthIf_CtrlModeIndication
*     - the switch/transceiver mode/timestamp stubs
*     - EthIf_GetIngressTimestamp and its callback pointer
*
*   Keeping all of the callbacks in one place means the project builds right
*   after code generation, with no manual deletion of generated code.
==================================================================================================*/


#ifdef __cplusplus
extern "C"{
#endif

/*==================================================================================================
*                                        INCLUDE FILES
==================================================================================================*/
#include "ComStackTypes.h"
#include "EthIf.h"
#include "Eth_43_GMAC.h"
#include "Eth_Stack.h"


/*==================================================================================================
*                              SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define ETHIF_VENDOR_ID_C                      43
#define ETHIF_AR_RELEASE_MAJOR_VERSION_C       4
#define ETHIF_AR_RELEASE_MINOR_VERSION_C       9
#define ETHIF_AR_RELEASE_REVISION_VERSION_C    0
#define ETHIF_SW_MAJOR_VERSION_C               7
#define ETHIF_SW_MINOR_VERSION_C               0
#define ETHIF_SW_PATCH_VERSION_C               1

/*==================================================================================================
*                                     FILE VERSION CHECKS
==================================================================================================*/
/* Check if current file and ETHIF header file are of the same vendor */
#if (ETHIF_VENDOR_ID_C != ETHIF_VENDOR_ID)
    #error "EthIf_App.c and EthIf.h have different vendor ids"
#endif
/* Check if current file and ETHIF header file are of the same Autosar version */
#if ((ETHIF_AR_RELEASE_MAJOR_VERSION_C    != ETHIF_AR_RELEASE_MAJOR_VERSION) || \
     (ETHIF_AR_RELEASE_MINOR_VERSION_C    != ETHIF_AR_RELEASE_MINOR_VERSION) || \
     (ETHIF_AR_RELEASE_REVISION_VERSION_C != ETHIF_AR_RELEASE_REVISION_VERSION))
    #error "AutoSar Version Numbers of EthIf_App.c and EthIf.h are different"
#endif
/* Check if current file and ETHIF header file are of the same Software version */
#if ((ETHIF_SW_MAJOR_VERSION_C != ETHIF_SW_MAJOR_VERSION) || \
     (ETHIF_SW_MINOR_VERSION_C != ETHIF_SW_MINOR_VERSION) || \
     (ETHIF_SW_PATCH_VERSION_C != ETHIF_SW_PATCH_VERSION))
    #error "Software Version Numbers of EthIf_App.c and EthIf.h are different"
#endif

/*==================================================================================================
*                                      GLOBAL VARIABLES
==================================================================================================*/
volatile uint32 EthIf_RxIndications[10]    = {0};
volatile uint32 EthIf_TxConfirmations[10]  = {0};
volatile boolean EthIf_ModeIndications[10] = {0};
volatile uint16 EthIf_ChecksumValue[10]    = {0};
Std_ReturnType (*EthIf_GetIngressTimestampCallback)(uint8, const Eth_DataType*, Eth_TimeStampQualType*, Eth_TimeStampType*) = &EthIf_GetIngressTimestamp;

/*==================================================================================================
*                                       GLOBAL FUNCTIONS
==================================================================================================*/

/*================================================================================================*/
/**
* @brief          Receive indication callback invoked by Eth_43_GMAC_Receive.
* @details        This is the real application receive path of the example. It
*                 replaces the empty stub generated in RTD/src/EthIf.c: the
*                 frame counter shown on the OLED is incremented, the frame is
*                 dispatched by EtherType (ARP / ICMP / TCP-HTTP) and the RX
*                 buffer is handed back to the DMA ring afterwards.
* @note           The passed data buffer is no longer valid after the function
*                 is exited.
* @param[in]      CtrlIdx Index of the controller which received the frame.
* @param[in]      FrameType The received frame Ethertype (from the frame header)
* @param[in]      IsBroadcast TRUE when the frame was sent to the broadcast
*                 address (ff-ff-ff-ff-ff-ff)
* @param[in]      PhysAddrPtr Pointer to the received frame source MAC address
*                 (6 bytes)
* @param[in]      DataPtr Data buffer containing the received Ethernet frame payload.
* @param[in]      DataLen Length of the data in the buffer DataPtr.
* @param[in]      IngressTimeTuplePtr Ingress timestamp tuple (unused here).
* @param[in]      RxHandleId Handle of the RX buffer that must be released.
*/
void EthIf_RxIndication
(
    uint8         CtrlIdx,
    Eth_FrameType FrameType,
    boolean       IsBroadcast,
    const uint8*  PhysAddrPtr,
    const Eth_DataType* DataPtr,
    uint16 DataLen
    ,TimeTupleType* IngressTimeTuplePtr
    ,Eth_BufIdxType RxHandleId
)
{
    (void)IsBroadcast;
    (void)IngressTimeTuplePtr;

    ++EthIf_RxIndications[CtrlIdx];

    /* Dispatch: ARP reply, ICMP echo reply, or HTTP/TCP response. */
    Eth_HandleFrame(CtrlIdx, FrameType, DataPtr, DataLen, PhysAddrPtr);

    /* Release the RX descriptor back to the DMA ring so it can receive new frames. */
    Eth_43_GMAC_ReleaseRxBuffer(CtrlIdx, RxHandleId);
}

/*================================================================================================*/

/**
* @brief          This function confirms that transmission of an Ethernet frame
*                 was finished.
* @param[in]      CtrlIdx Index of the controller which transmitted the frame.
* @param[in]      BufIdx Index of the transmitted data buffer.
*/
void EthIf_TxConfirmation(uint8 CtrlIdx, \
                          Eth_BufIdxType BufIdx, \
                          Std_ReturnType Result)
{
    ++EthIf_TxConfirmations[CtrlIdx];
    (void)BufIdx;
    (void)Result;
}

/*================================================================================================*/
/**
* @brief          This function indicates that driver mode has been changed.
* @param[in]      CtrlIdx Index of the controller which mode has been changed.
* @param[in]      CtrlMode New mode of correspond Eth driver.
*/
void EthIf_CtrlModeIndication( \
                                uint8 CtrlIdx, \
                                Eth_ModeType CtrlMode \
                             )
{
    EthIf_ModeIndications[CtrlIdx] = TRUE;
    (void)CtrlMode;
}

/*================================================================================================*/
/**
* @brief          This function indicates that an ingress timestamp was captured.
* @param[in]      CtrlIdx       Index of the controller which mode has been changed.
* @param[in]      MgmtInfoPtr   Management information
* @param[in]      timeStampPtr  Current timestamp
* @param[out]     DataPtr       Ethernet data pointer
*/
void EthIf_SwitchIngressTimeStampIndication(uint8 CtrlIdx,
                                            Eth_DataType* DataPtr,
                                            EthSwt_MgmtInfoType* MgmtInfoPtr,
                                            Eth_TimeStampType* timeStampPtr
                                           )
{
    (void)CtrlIdx;
    (void)DataPtr;
    (void)MgmtInfoPtr;
    (void)timeStampPtr;
}

/**
* @brief          This function indicates that an egress timestamp was captured.
* @param[in]      CtrlIdx       Index of the controller which mode has been changed.
* @param[intout]  DataPtr       Ethernet data pointer
* @param[out]     MgmtInfoPtr   Management information
* @param[out]     timeStampPtr  Current timestamp
*/
void EthIf_SwitchEgressTimeStampIndication(uint8 CtrlIdx,
                                           Eth_DataType* DataPtr,
                                           EthSwt_MgmtInfoType* MgmtInfoPtr,
                                           Eth_TimeStampType* timeStampPtr
                                          )
{
    (void)CtrlIdx;
    (void)DataPtr;
    (void)MgmtInfoPtr;
    (void)timeStampPtr;
}

/**
* @brief          This function indicates that mgmt information was received.
* @param[in]      CtrlIdx       Index of the controller which mode has been changed.
* @param[intout]  DataPtr       Ethernet data pointer
* @param[out]     MgmtInfoPtr   Management information
*/
void EthIf_SwitchMgmtInfoIndication(uint8 CtrlIdx,
                                    Eth_DataType* DataPtr,
                                    EthSwt_MgmtInfoType* MgmtInfoPtr
                                   )
{
    (void)CtrlIdx;
    (void)DataPtr;
    (void)MgmtInfoPtr;
}

/**
* @brief          This function indicates that a transceiver's mode was changed.
* @param[in]      TrcvIdx       Index of the Ethernet transceiver within the context of the Ethernet Interface
* @param[in]      TrcvMode      Notified Ethernet transceiver mode
*/
void EthIf_TrcvModeIndication(uint8 TrcvIdx,
                              EthTrcv_ModeType TrcvMode
                             )
{
    (void)TrcvIdx;
    (void)TrcvMode;
}

/**
* @brief          This function indicates that a switch port mode was changed.
* @param[in]      SwitchIdx     Index of the switch within the context of the Ethernet Switch Driver
* @param[in]      SwitchPortIdx Index of the port at the addressed switch.
* @param[in]      PortMode      Notified Ethernet Switch port mode.
*/
void EthIf_SwitchPortModeIndication(uint8 SwitchIdx,
                                    uint8 SwitchPortIdx,
                                    Eth_ModeType PortMode
                                   )
{
    (void)SwitchIdx;
    (void)SwitchPortIdx;
    (void)PortMode;
}

/**
* @brief          This function extracts Ingress Timestamps from received frames.
* @param[in]      CtrlIdx Index of the controller which received the frame.
* @param[in]      DataPtr Data buffer containing the received Ethernet frame payload.
* @param[in]      TimeQualPtr Pointer to timestamp quality indicator
* @param[in]      TimeStampPtr Pointer to timestamp structure to be populated
*/
Std_ReturnType EthIf_GetIngressTimestamp( \
                                         uint8 CtrlIdx, \
                                         const Eth_DataType *DataPtr, \
                                         Eth_TimeStampQualType* TimeQualPtr, \
                                         Eth_TimeStampType* TimeStampPtr \
                                        )
{
    (void)CtrlIdx;
    (void)DataPtr;
    (void)TimeQualPtr;
    (void)TimeStampPtr;
    return E_OK;
}

#ifdef __cplusplus
}
#endif
