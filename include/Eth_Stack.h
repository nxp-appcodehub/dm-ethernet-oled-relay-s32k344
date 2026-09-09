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
 *   Eth_Stack.h
 *   User-space ARP + ICMP echo responder for S32K344 / FRDM-A.
 *
 *   EthIf_RxIndication (RTD stub) delegates every received frame to
 *   Eth_HandleFrame so all application logic stays outside the RTD tree.
 ==================================================================================================*/
#ifndef ETH_STACK_H
#define ETH_STACK_H

#include "Eth_43_GMAC.h"

/* Called from EthIf_RxIndication for each received frame.
 * CtrlIdx   - GMAC controller index (0)
 * FrameType - EtherType extracted by the driver (0x0806 ARP, 0x0800 IPv4, ...)
 * DataPtr   - pointer to the frame payload (after the Ethernet header)
 * DataLen   - payload length in bytes                                          */
/* PhysAddrPtr - Ethernet source MAC from EthIf_RxIndication (needed by TCP) */
void Eth_HandleFrame(uint8 CtrlIdx, Eth_FrameType FrameType,
		const Eth_DataType *DataPtr, uint16 DataLen, const uint8 *PhysAddrPtr);

/* Debug counters - watch these in the debugger */
extern volatile uint32 Eth_ArpRequestsRx;
extern volatile uint32 Eth_ArpRepliesTx;
extern volatile uint32 Eth_IcmpRequestsRx;
extern volatile uint32 Eth_IcmpRepliesTx;

#endif /* ETH_STACK_H */
