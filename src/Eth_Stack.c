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
 *   Eth_Stack.c
 *   ARP + ICMP echo responder for S32K344 / FRDM-A-S32K344.
 *
 *   Network identity (edit the block below to match your setup):
 *     MCU  MAC  AB:AB:AB:AB:AB:AB   IP  192.168.0.101
 *
 *   ARP requests are answered for any sender MAC/IP as long as the target IP
 *   matches MCU_IP.  No PC_MAC or PC_IP restriction is applied.
 *
 *   Receive flow:
 *     EthIf_RxIndication (main.c)
 *       -> Eth_HandleFrame             (EtherType dispatch)
 *          -> HandleArp                (0x0806: send ARP reply)
 *          -> HandleIpv4               (0x0800: route by IP protocol)
 *             -> HandleIcmp            (proto 1: send ICMP echo reply)
 *             -> Http_HandleTcp        (proto 6: HTTP/TCP state machine)
 *
 *   ARP flow:
 *     PC sends ARP request  ->  board replies with its MAC address.
 *
 *   ICMP flow:
 *     PC sends ICMP echo request (ping)  ->  board replies with echo reply.
 ==================================================================================================*/

#include "Eth_Stack.h"
#include "Http_Server.h"

/*==================================================================================================
 *   NETWORK IDENTITY
 *   Update MCU_MAC / MCU_IP to match the board configuration in your
 *   EthConf tool settings and in Http_Server.c (BOARD_IP).
 *   No PC-side MAC or IP is required: ARP requests from any host are
 *   answered as long as they target MCU_IP.
 ==================================================================================================*/

/* MAC address programmed into the GMAC controller. */
static const uint8 MCU_MAC[6U] = { 0xABU, 0xABU, 0xABU, 0xABU, 0xABU, 0xABU };

/* IPv4 address of the board.  Must equal BOARD_IP in Http_Server.c. */
static const uint8 MCU_IP[4U] = { 192U, 168U, 0U, 101U };

/*==================================================================================================
 *   CONSTANTS
 ==================================================================================================*/

/* EtherType values used for frame dispatch. */
#define ETHERTYPE_ARP        (0x0806U)
#define ETHERTYPE_IPV4       (0x0800U)

/*
 * ARP payload field offsets (relative to the start of the ARP payload, i.e.
 * after the 14-byte Ethernet header which the GMAC driver has already stripped).
 *
 * Layout of a 28-byte ARP packet for Ethernet/IPv4:
 *   [0-1]   HW type    (0x0001 Ethernet)
 *   [2-3]   Proto type (0x0800 IPv4)
 *   [4]     HW addr len (6)
 *   [5]     Proto addr len (4)
 *   [6-7]   Opcode     (0x0001 request, 0x0002 reply)
 *   [8-13]  Sender HW addr (SHA)
 *   [14-17] Sender proto addr (SPA)
 *   [18-23] Target HW addr (THA)
 *   [24-27] Target proto addr (TPA)
 */
#define ARP_OPCODE_OFFSET    (6U)
#define ARP_OPCODE_REQUEST   (0x0001U)
#define ARP_MIN_LEN          (28U)

/* IP protocol numbers. */
#define IP_PROTO_ICMP        (0x01U)
#define IP_PROTO_TCP         (0x06U)

/* ICMP type codes used for echo request / reply. */
#define ICMP_TYPE_ECHO_REQ   (0x08U)
#define ICMP_TYPE_ECHO_REP   (0x00U)

/* Minimum lengths required before accessing header fields. */
#define IP_HDR_MIN_LEN       (20U)
#define ICMP_HDR_LEN         (8U)

/*==================================================================================================
 *   SHARED STATE
 *   The sender MAC/IP of the most recent valid ARP request is cached here so
 *   that ICMP reply frames know where to send their Ethernet frames without
 *   requiring a second ARP lookup.
 ==================================================================================================*/

/* MAC address of the last host that sent us a valid ARP request. */
static uint8 s_PeerMac[6U] = { 0U };

/* IPv4 address of the last host that sent us a valid ARP request. */
static uint8 s_PeerIp[4U] = { 0U };

/*==================================================================================================
 *   DEBUG COUNTERS
 *   Add these symbols as watch-point expressions in the debugger to monitor
 *   ARP and ICMP activity at run-time without adding printf or trace output.
 ==================================================================================================*/

/* Incremented for every ARP request frame received (any source). */
volatile uint32 Eth_ArpRequestsRx = 0U;

/* Incremented each time an ARP reply is successfully transmitted. */
volatile uint32 Eth_ArpRepliesTx = 0U;

/* Incremented for every valid ICMP echo request received. */
volatile uint32 Eth_IcmpRequestsRx = 0U;

/* Incremented each time an ICMP echo reply is successfully transmitted. */
volatile uint32 Eth_IcmpRepliesTx = 0U;

/*==================================================================================================
 *   HELPERS
 ==================================================================================================*/

/*
 * Checksum - standard one's-complement Internet checksum (RFC 1071).
 *
 * Used for both the IPv4 header checksum and the ICMP checksum.
 *
 * p   - pointer to the byte array to checksum
 * len - number of bytes to process
 *
 * Returns the 16-bit checksum value ready to be stored in the header field.
 */
static uint16 Checksum(const uint8 *p, uint16 len) {
	uint32 sum = 0U;
	uint16 i;

	/* Sum 16-bit words. */
	for (i = 0U; i + 1U < len; i += 2U) {
		sum += (uint32) (((uint16) p[i] << 8U) | (uint16) p[i + 1U]);
	}
	/* Add the remaining odd byte (padded with a zero byte on the right). */
	if ((len & 1U) != 0U) {
		sum += (uint32) ((uint16) p[len - 1U] << 8U);
	}
	/* Fold 32-bit accumulator into 16 bits. */
	while ((sum >> 16U) != 0U) {
		sum = (sum & 0xFFFFU) + (sum >> 16U);
	}
	return (uint16) (~sum);
}

/*==================================================================================================
 *   ARP HANDLER
 ==================================================================================================*/

/*
 * HandleArp - process an incoming ARP payload and send a reply if appropriate.
 *
 * Any ARP request (opcode 0x0001) that targets the board IP (MCU_IP) is
 * answered, regardless of the sender MAC or sender IP.  This allows any
 * host on the network to discover the board without pre-configuring its MAC
 * address in the firmware.
 *
 * CtrlIdx  - GMAC controller index passed through to the transmit call
 * DataPtr  - ARP payload (28 bytes for Ethernet/IPv4, opcode onwards)
 * DataLen  - payload length; checked against ARP_MIN_LEN before access
 */
static void HandleArp(uint8 CtrlIdx, const Eth_DataType *DataPtr,
		uint16 DataLen) {
	uint16 opcode;
	uint8 i;
	boolean dstOk = TRUE;
	Eth_BufIdxType BufIdx;
	Eth_DataType *Buf = NULL_PTR;
	uint16 Len = 28U;

	/* Discard undersized frames before accessing any field. */
	if (DataLen < ARP_MIN_LEN) {
		return;
	}

	/* Read the opcode field (big-endian, bytes 6-7). */
	opcode = (uint16) (((uint16) DataPtr[ARP_OPCODE_OFFSET] << 8U)
			| (uint16) DataPtr[ARP_OPCODE_OFFSET + 1U]);
	if (opcode != ARP_OPCODE_REQUEST) {
		return; /* Ignore ARP replies and any other non-request opcodes. */
	}

	++Eth_ArpRequestsRx;

	/*
	 * Accept any request where the target proto addr (bytes 24-27) matches
	 * MCU_IP.  Sender MAC and sender IP are not restricted: any host may
	 * resolve the board address.
	 */
	for (i = 0U; i < 4U; i++) {
		if (DataPtr[24U + i] != MCU_IP[i]) {
			dstOk = FALSE;
			break;
		}
	}

	if (dstOk != TRUE) {
		return;
	}

	/* Cache sender MAC/IP so ICMP replies can use them without a second ARP. */
	for (i = 0U; i < 6U; i++) {
		s_PeerMac[i] = DataPtr[8U + i];
	}
	for (i = 0U; i < 4U; i++) {
		s_PeerIp[i] = DataPtr[14U + i];
	}

	/* Acquire a TX descriptor from the GMAC driver. */
	if (Eth_43_GMAC_ProvideTxBuffer(CtrlIdx, 0U, &BufIdx, &Buf, &Len)
			!= BUFREQ_OK) {
		return;
	}
	if (Buf == NULL_PTR) {
		return;
	}

	/*
	 * Build the ARP reply payload (28 bytes) in the descriptor buffer:
	 *  [0-1]   HW type  0x0001 (Ethernet)
	 *  [2-3]   Proto    0x0800 (IPv4)
	 *  [4]     HW size  6
	 *  [5]     IP size  4
	 *  [6-7]   Opcode   0x0002 (reply)
	 *  [8-13]  Sender MAC = MCU_MAC   (board MAC - the answer)
	 *  [14-17] Sender IP  = MCU_IP    (board IP)
	 *  [18-23] Target MAC = s_PeerMac (MAC of the requester)
	 *  [24-27] Target IP  = s_PeerIp  (IP of the requester)
	 */
	Buf[0U] = 0x00U;
	Buf[1U] = 0x01U; /* HW type: Ethernet */
	Buf[2U] = 0x08U;
	Buf[3U] = 0x00U; /* Proto: IPv4 */
	Buf[4U] = 6U; /* HW addr length */
	Buf[5U] = 4U; /* IP addr length */
	Buf[6U] = 0x00U;
	Buf[7U] = 0x02U; /* Opcode: reply */

	for (i = 0U; i < 6U; i++) {
		Buf[8U + i] = MCU_MAC[i];
	}
	for (i = 0U; i < 4U; i++) {
		Buf[14U + i] = MCU_IP[i];
	}
	for (i = 0U; i < 6U; i++) {
		Buf[18U + i] = s_PeerMac[i];
	}
	for (i = 0U; i < 4U; i++) {
		Buf[24U + i] = s_PeerIp[i];
	}

	/* Transmit; the GMAC driver prepends the Ethernet header automatically. */
	(void) Eth_43_GMAC_Transmit(CtrlIdx, BufIdx, (Eth_FrameType) ETHERTYPE_ARP,
	FALSE, Len, s_PeerMac);
	++Eth_ArpRepliesTx;
}

/*==================================================================================================
 *   ICMP ECHO REPLY HANDLER
 ==================================================================================================*/

/*
 * HandleIcmp - send an ICMP echo reply for a received echo request.
 *
 * The function copies the received IPv4 datagram into a new TX buffer,
 * swaps source and destination IP addresses, resets the TTL, recomputes
 * the IP header checksum, changes the ICMP type from 8 (echo request) to
 * 0 (echo reply), and recomputes the ICMP checksum.  The payload (ping data)
 * is preserved unchanged.
 *
 * CtrlIdx  - GMAC controller index
 * DataPtr  - start of the IPv4 datagram (IP header + ICMP header + data)
 * DataLen  - total datagram length in bytes
 */
static void HandleIcmp(uint8 CtrlIdx, const Eth_DataType *DataPtr,
		uint16 DataLen) {
	uint8 ipHdrLen;
	uint16 icmpLen;
	uint16 csum;
	uint8 srcIp[4U], dstIp[4U];
	uint8 i;
	Eth_BufIdxType BufIdx;
	Eth_DataType *Buf = NULL_PTR;
	uint16 Len = DataLen;

	/* Need at least a complete IP header before reading any field. */
	if (DataLen < IP_HDR_MIN_LEN) {
		return;
	}

	/* IP header length from the IHL nibble (value in 32-bit words). */
	ipHdrLen = (uint8) ((DataPtr[0U] & 0x0FU) * 4U);

	/* Need IP header + ICMP header to be present. */
	if (DataLen < ((uint16) ipHdrLen + ICMP_HDR_LEN)) {
		return;
	}
	/* Verify this is an ICMP datagram. */
	if (DataPtr[9U] != IP_PROTO_ICMP) {
		return;
	}
	/* Verify the ICMP type is echo request (8). */
	if (DataPtr[(uint16) ipHdrLen] != ICMP_TYPE_ECHO_REQ) {
		return;
	}

	++Eth_IcmpRequestsRx;

	icmpLen = DataLen - (uint16) ipHdrLen;

	/*
	 * Swap source and destination IP addresses:
	 *   srcIp = DataPtr[16..19]  (original destination = our IP)
	 *   dstIp = DataPtr[12..15]  (original source = PC IP)
	 */
	for (i = 0U; i < 4U; i++) {
		srcIp[i] = DataPtr[16U + i];
	}
	for (i = 0U; i < 4U; i++) {
		dstIp[i] = DataPtr[12U + i];
	}

	/* Acquire a TX descriptor. */
	if (Eth_43_GMAC_ProvideTxBuffer(CtrlIdx, 0U, &BufIdx, &Buf, &Len)
			!= BUFREQ_OK) {
		return;
	}
	if (Buf == NULL_PTR) {
		return;
	}

	/* Copy the original IP header as a starting point. */
	for (i = 0U; i < ipHdrLen; i++) {
		Buf[i] = DataPtr[i];
	}

	/* Patch IP addresses (swapped) and reset TTL to 64. */
	for (i = 0U; i < 4U; i++) {
		Buf[12U + i] = srcIp[i]; /* New source: our IP */
	}
	for (i = 0U; i < 4U; i++) {
		Buf[16U + i] = dstIp[i]; /* New destination: PC IP */
	}
	Buf[8U] = 64U; /* TTL */

	/* Recompute IP header checksum (bytes 10-11 of the IP header). */
	Buf[10U] = 0x00U;
	Buf[11U] = 0x00U;
	csum = Checksum(Buf, (uint16) ipHdrLen);
	Buf[10U] = (uint8) (csum >> 8U);
	Buf[11U] = (uint8) (csum & 0xFFU);

	/* Copy the ICMP section (header + echo data) verbatim. */
	{
		uint16 j;
		for (j = 0U; j < icmpLen; j++) {
			Buf[(uint16) ipHdrLen + j] = DataPtr[(uint16) ipHdrLen + j];
		}
	}

	/* Change ICMP type from echo request (8) to echo reply (0). */
	Buf[(uint16) ipHdrLen + 0U] = ICMP_TYPE_ECHO_REP;
	Buf[(uint16) ipHdrLen + 1U] = 0x00U; /* Code: 0 */

	/* Zero the ICMP checksum field before recomputing it. */
	Buf[(uint16) ipHdrLen + 2U] = 0x00U;
	Buf[(uint16) ipHdrLen + 3U] = 0x00U;
	csum = Checksum(&Buf[(uint16) ipHdrLen], icmpLen);
	Buf[(uint16) ipHdrLen + 2U] = (uint8) (csum >> 8U);
	Buf[(uint16) ipHdrLen + 3U] = (uint8) (csum & 0xFFU);

	/* Transmit; GMAC driver prepends the Ethernet header. */
	(void) Eth_43_GMAC_Transmit(CtrlIdx, BufIdx, (Eth_FrameType) ETHERTYPE_IPV4,
	FALSE, Len, s_PeerMac);
	++Eth_IcmpRepliesTx;
}

/*==================================================================================================
 *   IPv4 DISPATCHER
 ==================================================================================================*/

/*
 * HandleIpv4 - route an IPv4 datagram to the appropriate protocol handler.
 *
 * Reads the IP protocol field (byte 9 of the IP header) and dispatches to:
 *   IP_PROTO_ICMP (1) -> HandleIcmp
 *   IP_PROTO_TCP  (6) -> Http_HandleTcp
 *   Any other value   -> silently discarded
 *
 * CtrlIdx      - GMAC controller index
 * DataPtr      - start of the IPv4 datagram
 * DataLen      - datagram length in bytes
 * PhysAddrPtr  - Ethernet source MAC (forwarded to Http_HandleTcp for TCP replies)
 */
static void HandleIpv4(uint8 CtrlIdx, const Eth_DataType *DataPtr,
		uint16 DataLen, const uint8 *PhysAddrPtr) {
	uint8 ipHdrLen;

	if (DataLen < IP_HDR_MIN_LEN) {
		return;
	}
	ipHdrLen = (uint8) ((DataPtr[0U] & 0x0FU) * 4U);

	switch (DataPtr[9U]) {
	case IP_PROTO_ICMP:
		HandleIcmp(CtrlIdx, DataPtr, DataLen);
		break;

	case IP_PROTO_TCP:
		/* Forward the full IPv4 datagram and sender MAC to the HTTP server. */
		Http_HandleTcp(CtrlIdx, DataPtr, DataLen, PhysAddrPtr);
		break;

	default:
		/* Unknown protocol: discard silently. */
		break;
	}
	(void) ipHdrLen; /* Suppress unused-variable warning; field reserved for future use. */
}

/*==================================================================================================
 *   PUBLIC API
 ==================================================================================================*/

/*
 * Eth_HandleFrame - top-level frame dispatcher.  See Eth_Stack.h for details.
 *
 * Performs a NULL/empty-length guard then switches on the EtherType to
 * route the frame to the ARP or IPv4 sub-handler.
 */
void Eth_HandleFrame(uint8 CtrlIdx, Eth_FrameType FrameType,
		const Eth_DataType *DataPtr, uint16 DataLen, const uint8 *PhysAddrPtr) {
	if ((DataPtr == NULL_PTR) || (DataLen == 0U)) {
		return;
	}

	switch (FrameType) {
	case (Eth_FrameType) ETHERTYPE_ARP:
		HandleArp(CtrlIdx, DataPtr, DataLen);
		break;

	case (Eth_FrameType) ETHERTYPE_IPV4:
		HandleIpv4(CtrlIdx, DataPtr, DataLen, PhysAddrPtr);
		break;

	default:
		/* Unsupported EtherType: discard silently. */
		break;
	}
}
