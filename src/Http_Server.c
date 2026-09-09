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
 * @file    Http_Server.c
 * @brief   Lightweight HTTP/1.0 server over a bare-metal TCP state machine.
 *
 * Design overview
 * ---------------
 * This module implements the minimum TCP/IP behaviour needed to serve a single
 * HTTP GET response to a browser.  No OS, no heap, no external TCP stack.
 *
 * The TCP state machine tracks one connection at a time through four states:
 *   TCP_LISTEN      - Waiting for an incoming SYN.
 *   TCP_SYN_RCVD    - SYN received; SYN-ACK sent; waiting for the final ACK.
 *   TCP_ESTABLISHED - Three-way handshake complete; waiting for HTTP data.
 *   TCP_FIN_WAIT    - HTTP response + FIN sent; waiting for client FIN/ACK.
 *
 * Frame path (receive side):
 *   EthIf_RxIndication -> Eth_HandleFrame -> Http_HandleTcp -> BuildAndSendTcp
 *
 * TX path:
 *   BuildAndSendTcp builds an IP+TCP segment in a static 1 KB buffer, acquires
 *   a GMAC TX descriptor via Eth_43_GMAC_ProvideTxBuffer, copies the segment
 *   into the descriptor buffer, and calls Eth_43_GMAC_Transmit.  The GMAC
 *   driver prepends the Ethernet header automatically.
 *
 * Limitations:
 *   - Single connection, no retransmission, no fragmentation.
 *   - HTTP response must fit in one Ethernet frame (<= 1500 B payload).
 *   - ISN is a fixed constant (sufficient for a demo; not suitable for
 *     production security-sensitive applications).
 *
 * Network identity:
 *   Board IP: 192.168.0.101  (BOARD_IP below)
 *   HTTP port: 80            (HTTP_SERVER_PORT in Http_Server.h)
 *   Both must match the configuration in Eth_Stack.c.
 */

#include "Http_Server.h"
#include "App_Commands.h"
#include <string.h>

/** @brief Command mailbox definition - set here, consumed by main.c. */
volatile uint8 g_AppPendingCommand = 0U;

/*==================================================================================================
 *   NETWORK IDENTITY
 *   BOARD_IP is embedded in every outgoing IP header as the source address.
 *   The Ethernet source MAC is filled in automatically by the GMAC driver.
 *   Update this constant to match your network setup; it must equal MCU_IP in
 *   Eth_Stack.c.
 ==================================================================================================*/
static const uint8 BOARD_IP[4U] = { 192U, 168U, 0U, 101U };

/*==================================================================================================
 *   CONSTANTS
 ==================================================================================================*/

/** EtherType for IPv4 frames. */
#define ETHERTYPE_IPV4  (0x0800U)

/** IP protocol number for TCP. */
#define IP_PROTO_TCP    (6U)

/* TCP control flag bits (RFC 793). */
#define TCP_FIN  (0x01U)  /**< No more data from sender. */
#define TCP_SYN  (0x02U)  /**< Synchronise sequence numbers. */
#define TCP_RST  (0x04U)  /**< Reset the connection. */
#define TCP_PSH  (0x08U)  /**< Push buffered data to the application. */
#define TCP_ACK  (0x10U)  /**< Acknowledgement field is significant. */

/** Fixed length of the IP header used by this implementation (no options). */
#define IP_HDR_LEN   (20U)

/** Fixed length of the TCP header used by this implementation (no options). */
#define TCP_HDR_LEN  (20U)

/**
 * @brief   Board initial sequence number (ISN).
 *
 * A fixed ISN is acceptable for a demonstration.  A production implementation
 * should derive the ISN from a hardware random number generator or a time-
 * based algorithm (RFC 6528) to prevent sequence-number prediction attacks.
 */
#define ISN          (0x12345678UL)

/**
 * @brief   Size of the static TX assembly buffer in bytes.
 *
 * Must be large enough to hold one IP header + TCP header + the complete
 * HTTP response.  The value must not exceed the maximum Ethernet payload
 * size (1500 B) because the response is sent in a single GMAC descriptor.
 */
#define TX_BUF_SIZE  (1024U)

/*==================================================================================================
 *   BYTE-ORDER HELPERS
 *   The S32K344 is little-endian; network byte order is big-endian.
 *   All multi-byte fields in IP and TCP headers must be converted.
 ==================================================================================================*/

/** Swap bytes in a 16-bit value (host <-> network). */
static inline uint16 bswap16(uint16 x) {
	return (uint16) (((uint16) (x) << 8U) | ((uint16) (x) >> 8U));
}

/** Swap bytes in a 32-bit value (host <-> network). */
static inline uint32 bswap32(uint32 x) {
	return ((x << 24U) | ((x & 0xFF00UL) << 8U) | ((x & 0xFF0000UL) >> 8U)
			| (x >> 24U));
}

/** Convert a 16-bit value from host byte order to network byte order. */
#define HTONS(x) bswap16((uint16)(x))

/** Convert a 32-bit value from host byte order to network byte order. */
#define HTONL(x) bswap32((uint32)(x))

/** Convert a 16-bit value from network byte order to host byte order. */
#define NTOHS(x) bswap16((uint16)(x))

/** Convert a 32-bit value from network byte order to host byte order. */
#define NTOHL(x) bswap32((uint32)(x))

/*==================================================================================================
 *   PACKED STRUCTS
 *   The GMAC driver provides the raw IP datagram starting directly after the
 *   14-byte Ethernet header, so these structs overlay the received/transmitted
 *   buffers without an Ethernet prefix.
 ==================================================================================================*/

/**
 * @brief   IPv4 header layout (20 bytes, no options).
 *
 * All multi-byte fields are in network byte order as received/transmitted.
 * Use the HTONS/NTOHS macros before reading or writing them.
 */
typedef struct __attribute__((packed)) {
	uint8 ver_ihl; /**< Version (4) and IHL (5 = 20 B) packed in one byte. */
	uint8 tos; /**< Type of service / DSCP+ECN. */
	uint16 total_len; /**< Total datagram length including IP header (network order). */
	uint16 id; /**< Identification field for fragmentation. */
	uint16 flags_frag; /**< Flags (DF, MF) and fragment offset (network order). */
	uint8 ttl; /**< Time to live. */
	uint8 proto; /**< Encapsulated protocol (6 = TCP). */
	uint16 hdr_csum; /**< IP header checksum (network order). */
	uint8 src[4U]; /**< Source IPv4 address. */
	uint8 dst[4U]; /**< Destination IPv4 address. */
} IpHdr_t;

/**
 * @brief   TCP header layout (20 bytes, no options).
 *
 * All multi-byte fields are in network byte order as received/transmitted.
 */
typedef struct __attribute__((packed)) {
	uint16 src_port; /**< Source port number (network order). */
	uint16 dst_port; /**< Destination port number (network order). */
	uint32 seq; /**< Sequence number (network order). */
	uint32 ack_seq; /**< Acknowledgement number (network order). */
	uint8 data_off; /**< Data offset: header length in 32-bit words, in the upper nibble. */
	uint8 flags; /**< TCP control flags (FIN, SYN, RST, PSH, ACK, ...). */
	uint16 window; /**< Receive window size (network order). */
	uint16 csum; /**< TCP checksum (network order). */
	uint16 urgent; /**< Urgent pointer (network order, zero if URG not set). */
} TcpHdr_t;

/*==================================================================================================
 *   TCP STATE MACHINE
 ==================================================================================================*/

/**
 * @brief   TCP connection states tracked by the server.
 */
typedef enum {
	TCP_LISTEN = 0, /**< No active connection; waiting for a SYN. */
	TCP_SYN_RCVD, /**< SYN received; SYN-ACK sent; awaiting final ACK. */
	TCP_ESTABLISHED, /**< Handshake complete; awaiting HTTP request data. */
	TCP_FIN_WAIT /**< HTTP response + FIN sent; awaiting client FIN. */
} TcpState_t;

/** Ethernet MAC address of the currently connected client (6 bytes). */
static uint8 s_PeerMac[6U];

/** IPv4 address of the currently connected client (4 bytes). */
static uint8 s_PeerIp[4U];

/** TCP source port of the currently connected client (host byte order). */
static uint16 s_PeerPort;

/**
 * @brief   Next expected sequence number from the client (host byte order).
 *
 * Updated each time a segment carrying data or a SYN/FIN (which each consume
 * one sequence number) is received from the peer.
 */
static uint32 s_RemoteSeq;

/*==================================================================================================
 *   TEMPERATURE PAGE
 ==================================================================================================*/

/** @brief Scaling of the P3T1750DP code: 0.0625 degC per LSB -> 0.0001 degC. */
#define TEMP_LSB_SCALE_1E4      625

/** @brief Divisor taking 0.0001 degC units down to the 0.1 degC shown on the page. */
#define TEMP_SCALE_1E1_DIV      1000

/** @brief Number of tenths in one whole degree. */
#define TEMP_SCALE_1E1          10


/** @brief Latest temperature code published by Http_SetTemperature(). */
static sint16 s_TempRawCode = 0;

/** @brief FALSE until the first reading arrives, so the page can say "n/a". */
static boolean s_TempValid = FALSE;

/** @brief Browser auto-refresh interval of the temperature page, in seconds. */
#define TEMP_PAGE_REFRESH_S     "5"

/**
 * @brief   Length of the Ethernet header the GMAC driver prepends, in bytes.
 *
 * Destination MAC (6) + source MAC (6) + EtherType (2), matching
 * ETH_43_GMAC_FRAME_HEADER_LENGTH inside the driver. These bytes have to be
 * counted against the transmit buffer even though this module never writes
 * them: Eth_43_GMAC_ProvideTxBuffer() computes
 *   FrameLength = requested payload + ETH_43_GMAC_FRAME_HEADER_LENGTH
 * and Gmac_Ip_GetTxBuff() then refuses the request when FrameLength exceeds the
 * descriptor buffer length. Omitting them makes the driver drop the response
 * while the TCP handshake still succeeds, so a client connects, sends its
 * request, and then receives nothing at all.
 */
#define ETH_HDR_LEN             (14U)

/**
 * @brief   Size of one GMAC transmit buffer, in bytes.
 *
 * Mirrors GMAC_0_MAX_TXBUFFLEN_SUPPORTED in the generated GMAC configuration,
 * which the Egress queue BufLenByte setting in the .mex drives. Restated here
 * rather than included so this module keeps depending only on the public Eth
 * interface; the assertions below catch any mismatch, which would otherwise
 * show up as a page that never loads.
 */
#define GMAC_TX_BUFFER_LEN      (128U)

/**
 * @brief   Upper bound for the generated HTTP response, headers included.
 *
 * The whole reply travels in a single TCP segment, so the Ethernet, IP and TCP
 * headers plus the response all have to fit one GMAC transmit buffer. With a
 * 128-byte buffer that leaves 128 - 14 - 20 - 20 = 74 bytes for the response.
 *
 * The page below is measured at 68 bytes worst case. Keep the markup terse when
 * editing it; to buy real headroom, raise the Egress queue BufLenByte in the
 * .mex (and GMAC_TX_BUFFER_LEN above) to 1536.
 */
#define TEMP_PAGE_RESP_MAX \
    (GMAC_TX_BUFFER_LEN - ETH_HDR_LEN - IP_HDR_LEN - TCP_HDR_LEN)

/* Guard both limits: the local assembly buffer and one GMAC transmit buffer,
 * the latter including the Ethernet header the driver adds. */
#if ((TEMP_PAGE_RESP_MAX + IP_HDR_LEN + TCP_HDR_LEN) > TX_BUF_SIZE)
#error "Temperature page too large for the TX assembly buffer"
#endif
#if ((TEMP_PAGE_RESP_MAX + ETH_HDR_LEN + IP_HDR_LEN + TCP_HDR_LEN) > GMAC_TX_BUFFER_LEN)
#error "Temperature page too large for a single GMAC transmit buffer"
#endif

/** @brief Assembly buffer for the generated HTTP response (headers + HTML). */
static char s_PageBuf[TEMP_PAGE_RESP_MAX];

/**
 * @brief   Cache the newest temperature reading.  See Http_Server.h.
 */
void Http_SetTemperature(sint16 RawCode) {
	s_TempRawCode = RawCode;
	s_TempValid = TRUE;
}

/**
 * @brief   Append an unsigned decimal number to a buffer.
 * @details Hand-rolled instead of using snprintf() so the response path pulls
 *          in no formatted-output code from the C library.
 * @param[out] pBuf   Destination buffer.
 * @param[in]  Index  Offset to start writing at.
 * @param[in]  Value  Value to render.
 * @return     New offset just past the digits written.
 */
static uint32 AppendUint(char *pBuf, uint32 Index, uint32 Value) {
	char digits[10];
	uint32 count = 0U;
	uint32 pos = Index;

	do {
		digits[count] = (char) ('0' + (char) (Value % 10U));
		Value = Value / 10U;
		count++;
	} while ((Value > 0U) && (count < (uint32) sizeof(digits)));

	while (count > 0U) {
		count--;
		pBuf[pos] = digits[count];
		pos++;
	}

	return pos;
}

/**
 * @brief   Append a null-terminated string to a buffer.
 * @param[out] pBuf   Destination buffer.
 * @param[in]  Index  Offset to start writing at.
 * @param[in]  pStr   Source string, copied without its terminator.
 * @return     New offset just past the copied text.
 */
static uint32 AppendStr(char *pBuf, uint32 Index, const char *pStr) {
	uint32 pos = Index;

	while (*pStr != '\0') {
		pBuf[pos] = *pStr;
		pos++;
		pStr++;
	}

	return pos;
}

/**
 * @brief   Render the temperature into "-12.3" style text.
 * @details Produces one decimal place. Emits "--.-" when no reading has been
 *          published yet, so a browser never sees a fabricated 0.0 value.
 * @param[out] pBuf   Destination buffer.
 * @param[in]  Index  Offset to start writing at.
 * @return     New offset just past the rendered value.
 */
static uint32 AppendTemperature(char *pBuf, uint32 Index) {
	sint32 tenths;
	uint32 magnitude;
	uint32 pos = Index;

	if (s_TempValid != TRUE) {
		return AppendStr(pBuf, pos, "--.-");
	}

	/* Truncates toward zero, which is fine for a single displayed decimal. */
	tenths = ((sint32) s_TempRawCode * TEMP_LSB_SCALE_1E4) / TEMP_SCALE_1E1_DIV;

	if (tenths < 0) {
		pBuf[pos] = '-';
		pos++;
		magnitude = (uint32) (-tenths);
	} else {
		magnitude = (uint32) tenths;
	}

	pos = AppendUint(pBuf, pos, magnitude / (uint32) TEMP_SCALE_1E1);
	pBuf[pos] = '.';
	pos++;
	pos = AppendUint(pBuf, pos, magnitude % (uint32) TEMP_SCALE_1E1);

	return pos;
}

/**
 * @brief   Build the complete HTTP response carrying the temperature page.
 * @details Only TEMP_PAGE_RESP_MAX (74) bytes are available, so the reply is
 *          stripped to the minimum a browser still renders:
 *            - No Content-Type. Browsers sniff the markup, and dropping the
 *              header frees 25 bytes.
 *            - No Content-Length. The FIN sent with this segment already marks
 *              the end of the body.
 *            - No doctype, no html/head/body tags and no heading tags, all of
 *              which browsers infer or ignore.
 *          What remains is the <meta refresh> that reloads the reading every
 *          few seconds plus the value itself: 19 bytes of status line, 35 of
 *          refresh tag and up to 14 of text, 68 bytes worst case.
 *
 *          A response that outgrew the buffer would be dropped by the driver
 *          and the page would silently fail to load, so the assembled length is
 *          still checked and a shorter fallback sent if it ever does not fit.
 *
 * @param[out] pLen  Receives the total response length in bytes.
 * @return     Pointer to the response bytes inside s_PageBuf.
 */
static const char *BuildTemperaturePage(uint16 *pLen) {
	static const char FALLBACK[] = "HTTP/1.0 200 OK\r\n\r\nTemp n/a";
	uint32 pos = 0U;

	pos = AppendStr(s_PageBuf, 0U,
			"HTTP/1.0 200 OK\r\n\r\n"
			"<meta http-equiv=refresh content=" TEMP_PAGE_REFRESH_S ">"
			"Temp: ");
	pos = AppendTemperature(s_PageBuf, pos);
	pos = AppendStr(s_PageBuf, pos, " C");

	/* Never transmit past the end of the buffer. */
	if (pos > (uint32) sizeof(s_PageBuf)) {
		(void) memcpy(s_PageBuf, FALLBACK, sizeof(FALLBACK) - 1U);
		pos = (uint32) (sizeof(FALLBACK) - 1U);
	}

	*pLen = (uint16) pos;
	return s_PageBuf;
}

/*==================================================================================================
 *   CHECKSUM FUNCTIONS
 ==================================================================================================*/

/**
 * @brief   Compute the standard one's-complement Internet checksum (RFC 1071).
 *
 * Used for both the IP header checksum and (as part of TcpChecksum) the TCP
 * segment checksum.
 *
 * @param[in]  data  Pointer to the byte array to checksum.
 * @param[in]  len   Number of bytes to include in the calculation.
 * @return           16-bit one's-complement checksum in network byte order.
 */
static uint16 IpChecksum(const void *data, uint16 len) {
	const uint8 *p = (const uint8*) data;
	uint32 sum = 0UL;
	uint16 i;
	for (i = 0U; (i + 1U) < len; i += 2U) {
		sum += (uint32) ((uint16) p[i] << 8U) | (uint32) p[i + 1U];
	}
	if (len & 1U) {
		sum += (uint32) p[len - 1U] << 8U;
	}
	while (sum >> 16U) {
		sum = (sum & 0xFFFFUL) + (sum >> 16U);
	}
	return (uint16) (~sum);
}

/**
 * @brief   Compute the TCP checksum using the IPv4 pseudo-header (RFC 793).
 *
 * The pseudo-header consists of: source IP (4 B), destination IP (4 B),
 * zero byte, protocol byte (6), and TCP segment length (2 B).  The checksum
 * covers the pseudo-header followed by the entire TCP segment (header + data).
 *
 * @param[in]  srcIp   Pointer to the 4-byte source IPv4 address.
 * @param[in]  dstIp   Pointer to the 4-byte destination IPv4 address.
 * @param[in]  tcpSeg  Pointer to the start of the TCP header.
 * @param[in]  tcpLen  Length of the TCP segment (header + data) in bytes.
 * @return             16-bit checksum to be written into the TCP csum field
 *                     (already in the correct byte order for direct storage).
 */
static uint16 TcpChecksum(const uint8 *srcIp, const uint8 *dstIp,
		const uint8 *tcpSeg, uint16 tcpLen) {
	uint8 pseudo[12U];
	uint32 sum = 0UL;
	uint16 i;

	/* Build the 12-byte IPv4 pseudo-header. */
	memcpy(pseudo, srcIp, 4U);
	memcpy(pseudo + 4U, dstIp, 4U);
	pseudo[8U] = 0U;
	pseudo[9U] = IP_PROTO_TCP;
	pseudo[10U] = (uint8) (tcpLen >> 8U);
	pseudo[11U] = (uint8) (tcpLen);

	/* Accumulate pseudo-header. */
	for (i = 0U; (i + 1U) < 12U; i += 2U) {
		sum += (uint32) ((uint16) pseudo[i] << 8U) | (uint32) pseudo[i + 1U];
	}

	/* Accumulate TCP segment. */
	for (i = 0U; (i + 1U) < tcpLen; i += 2U) {
		sum += (uint32) ((uint16) tcpSeg[i] << 8U) | (uint32) tcpSeg[i + 1U];
	}
	if (tcpLen & 1U) {
		sum += (uint32) tcpSeg[tcpLen - 1U] << 8U;
	}

	/* Fold 32-bit accumulator into 16 bits. */
	while (sum >> 16U) {
		sum = (sum & 0xFFFFUL) + (sum >> 16U);
	}
	return (uint16) (~sum);
}

/*==================================================================================================
 *   FRAME BUILDER / SENDER
 ==================================================================================================*/

/**
 * @brief   Build an IP+TCP segment and transmit it via the GMAC driver.
 *
 * The function assembles a complete IPv4 datagram (IP header + TCP header +
 * optional payload) into a static assembly buffer, acquires a GMAC TX
 * descriptor, copies the datagram into the descriptor buffer, and calls
 * Eth_43_GMAC_Transmit.  The GMAC driver prepends the Ethernet header
 * (destination MAC = s_PeerMac, source MAC = board MAC from driver config,
 * EtherType = 0x0800).
 *
 * Both IP header checksum and TCP checksum are calculated before transmission.
 *
 * @param[in]  CtrlIdx     GMAC controller index (0).
 * @param[in]  tcpFlags    TCP control flags byte (combination of TCP_SYN,
 *                         TCP_ACK, TCP_PSH, TCP_FIN, TCP_RST).
 * @param[in]  seq         TCP sequence number to place in the outgoing segment
 *                         (host byte order; converted to network order here).
 * @param[in]  ack         TCP acknowledgement number (host byte order).
 * @param[in]  payload     Pointer to the TCP payload data, or NULL if none.
 * @param[in]  payloadLen  Length of the payload in bytes (0 if payload==NULL).
 * @return                 Total number of bytes transmitted (IP header + TCP
 *                         header + payload), or 0 on failure (buffer too large
 *                         or ProvideTxBuffer returned BUFREQ_BUSY).
 */
static uint16 BuildAndSendTcp(uint8 CtrlIdx, uint8 tcpFlags, uint32 seq,
		uint32 ack, const uint8 *payload, uint16 payloadLen) {
	static uint8 txBuf[TX_BUF_SIZE];
	IpHdr_t *ip = (IpHdr_t*) (void*) txBuf;
	TcpHdr_t *tcp = (TcpHdr_t*) (void*) (txBuf + IP_HDR_LEN);

	uint16 tcpSegLen = (uint16) (TCP_HDR_LEN + payloadLen);
	uint16 ipTotalLen = (uint16) (IP_HDR_LEN + tcpSegLen);
	uint16 csum;

	/* Guard: assembled frame must fit inside the static buffer. */
	if (ipTotalLen > TX_BUF_SIZE) {
		return 0U;
	}

	/* --- IP header --- */
	ip->ver_ihl = 0x45U; /* IPv4, IHL = 5 (20-byte header, no options). */
	ip->tos = 0U;
	ip->total_len = HTONS(ipTotalLen);
	ip->id = 0U;
	ip->flags_frag = HTONS(0x4000U); /* DF bit set; no fragmentation. */
	ip->ttl = 64U;
	ip->proto = IP_PROTO_TCP;
	ip->hdr_csum = 0U; /* Zero before checksum calculation. */
	memcpy(ip->src, BOARD_IP, 4U);
	memcpy(ip->dst, s_PeerIp, 4U);
	ip->hdr_csum = HTONS(IpChecksum(ip, IP_HDR_LEN));

	/* --- TCP header --- */
	tcp->src_port = HTONS(HTTP_SERVER_PORT);
	tcp->dst_port = HTONS(s_PeerPort);
	tcp->seq = HTONL(seq);
	tcp->ack_seq = HTONL(ack);
	tcp->data_off = (uint8) (5U << 4U); /* Data offset = 5 (20-byte header, no options). */
	tcp->flags = tcpFlags;
	tcp->window = HTONS(1460U); /* Advertise a 1460-byte receive window. */
	tcp->csum = 0U; /* Zero before checksum calculation. */
	tcp->urgent = 0U;

	/* Copy payload immediately after the TCP header. */
	if ((payloadLen > 0U) && (payload != NULL)) {
		memcpy(txBuf + IP_HDR_LEN + TCP_HDR_LEN, payload, payloadLen);
	}

	/* Compute and fill in the TCP checksum. */
	csum = TcpChecksum(BOARD_IP, s_PeerIp, (const uint8*) tcp, tcpSegLen);
	tcp->csum = HTONS(csum);

	/* Acquire a GMAC TX descriptor and transmit. */
	{
		Eth_BufIdxType BufIdx;
		Eth_DataType *BufPtr = NULL_PTR;
		uint16 BufLen = ipTotalLen;

		if (Eth_43_GMAC_ProvideTxBuffer(CtrlIdx, 0U, &BufIdx, &BufPtr, &BufLen)
				!= BUFREQ_OK) {
			return 0U;
		}
		if (BufPtr == NULL_PTR) {
			return 0U;
		}

		memcpy(BufPtr, txBuf, ipTotalLen);
		(void) Eth_43_GMAC_Transmit(CtrlIdx, BufIdx,
				(Eth_FrameType) ETHERTYPE_IPV4, FALSE, ipTotalLen, s_PeerMac);
	}
	return ipTotalLen;
}

/*==================================================================================================
 *   PUBLIC API
 ==================================================================================================*/

/**
 * @brief   Process an incoming TCP segment directed at the HTTP server.
 *          Full description in Http_Server.h.
 */
void Http_HandleTcp(uint8 CtrlIdx, const Eth_DataType *DataPtr, uint16 DataLen,
		const uint8 *PeerMac) {
	/*
	 * Direct byte-index parsing of the received IPv4 + TCP frame. DataPtr
	 * points to the start of the IPv4 datagram (no Ethernet header). Fixed
	 * offsets inside the IP header are safe to index directly:
	 *   [0]        Version and IHL; the low nibble gives the IP header length
	 *   [9]        IP protocol (must be 6 = TCP)
	 *   [12..15]   IP source address (the peer)
	 *   [16..19]   IP destination address (must be 192.168.0.101)
	 *
	 * Everything in the TCP header is reached through ipHdrLen, and the payload
	 * through tcpHdrLen, because neither length is fixed: IP may carry options
	 * (IHL > 5) and TCP almost always does on the first segments (MSS, SACK,
	 * window scale, timestamps). The previous version assumed a 20+20 layout
	 * and read the request at a hardcoded offset 40, which lands inside the TCP
	 * options instead of the payload as soon as any option is present.
	 */
	uint8 myAppBuffer[1500] = { 0 };
	uint8 flags;
	uint32 remoteSeq;
	uint16 ipHdrLen;
	uint16 tcpHdrLen;
	uint16 hdrTotalLen;
	uint16 tcpDataLen;

	if ((DataPtr == NULL_PTR) || (DataLen < 40U)) {
		return;
	}

	/* Clamp to one byte below the buffer size: the request text is terminated
	 * in place further down, so the last slot has to stay free. */
	if (DataLen > (uint16) (sizeof(myAppBuffer) - 1U)) {
		DataLen = (uint16) (sizeof(myAppBuffer) - 1U);
	}
	memcpy(myAppBuffer, DataPtr, DataLen);

	/* 1. Protocol must be TCP (6). */
	if (myAppBuffer[9] != 6U) {
		return;
	}

	/* 2. IP header length from the IHL nibble, in 32-bit words. A valid IPv4
	 *    header is at least 5 words (20 bytes). */
	ipHdrLen = (uint16) ((myAppBuffer[0] & 0x0FU) * 4U);
	if ((ipHdrLen < IP_HDR_LEN) || ((uint16) (ipHdrLen + TCP_HDR_LEN) > DataLen)) {
		return;
	}

	/* 3. TCP header length from the data offset nibble, also in 32-bit words. */
	tcpHdrLen = (uint16) (((myAppBuffer[ipHdrLen + 12U] >> 4U) & 0x0FU) * 4U);
	if (tcpHdrLen < TCP_HDR_LEN) {
		return;
	}

	hdrTotalLen = (uint16) (ipHdrLen + tcpHdrLen);
	if (hdrTotalLen > DataLen) {
		return;
	}
	tcpDataLen = (uint16) (DataLen - hdrTotalLen);

	/* 4. Destination IP must be this board (192.168.0.101). */
	if ((myAppBuffer[16] != 192U) || (myAppBuffer[17] != 168U)
			|| (myAppBuffer[18] != 0U) || (myAppBuffer[19] != 101U)) {
		return;
	}

	/* 5. Destination port must be 80 (0x00 0x50). */
	if ((myAppBuffer[ipHdrLen + 2U] != 0x00U)
			|| (myAppBuffer[ipHdrLen + 3U] != 0x50U)) {
		return;
	}

	/* Cache the peer identity for the reply. */
	memcpy(s_PeerMac, PeerMac, 6U);
	s_PeerIp[0] = myAppBuffer[12];
	s_PeerIp[1] = myAppBuffer[13];
	s_PeerIp[2] = myAppBuffer[14];
	s_PeerIp[3] = myAppBuffer[15];
	s_PeerPort = (uint16) (((uint16) myAppBuffer[ipHdrLen] << 8U)
			| myAppBuffer[ipHdrLen + 1U]);

	/* Client sequence number (big endian) from the TCP header. */
	remoteSeq = ((uint32) myAppBuffer[ipHdrLen + 4U] << 24U)
			| ((uint32) myAppBuffer[ipHdrLen + 5U] << 16U)
			| ((uint32) myAppBuffer[ipHdrLen + 6U] << 8U)
			| ((uint32) myAppBuffer[ipHdrLen + 7U]);

	/* 6. TCP flags. */
	flags = myAppBuffer[ipHdrLen + 13U];

	/*
	 * IMPORTANT: keep the connection stateless with respect to our local
	 * sequence number.  A browser opens several parallel TCP connections
	 * (page + favicon + keep-alive) that all share these global variables.
	 * Because we always send the whole response (data + FIN) in a single
	 * segment, every connection can safely use the same fixed numbering:
	 *   SYN-ACK -> seq = ISN
	 *   data    -> seq = ISN + 1
	 * Never accumulate s_LocalSeq across packets, otherwise a second parallel
	 * connection would receive a segment with the wrong sequence number and
	 * the client would keep retransmitting (the "TCP Retransmission" you saw).
	 */

	/*
	 * SYN without ACK: open a new connection -> reply with SYN-ACK.
	 *
	 * Test the SYN bit rather than comparing the whole flags byte to 0x02.
	 * Modern clients negotiate ECN (RFC 3168) by setting ECE and CWR on the
	 * initial SYN, which makes the byte 0xC2; an exact comparison misses that
	 * and the handshake never starts, while ARP and ICMP continue to work.
	 * ACK must be absent so this does not also match the SYN-ACK case.
	 */
	if (((flags & TCP_SYN) != 0U) && ((flags & TCP_ACK) == 0U)) {
		s_RemoteSeq = remoteSeq + 1UL; /* SYN consumes one sequence number. */
		(void) BuildAndSendTcp(CtrlIdx, TCP_SYN | TCP_ACK, ISN, s_RemoteSeq,
				NULL, 0U);
		return;
	}

	/* FIN from client: acknowledge so it does not retransmit its FIN. */
	if ((flags & TCP_FIN) != 0U) {
		s_RemoteSeq = remoteSeq + tcpDataLen + 1UL; /* FIN consumes one seq. */
		(void) BuildAndSendTcp(CtrlIdx, TCP_ACK, ISN + 1UL + 1UL, s_RemoteSeq,
				NULL, 0U);
		return;
	}

	/* ACK / data segment. */
	if ((flags & TCP_ACK) != 0U) {
		/* Look for "GET" at the start of the TCP payload. */
		if ((tcpDataLen >= 3U) && (myAppBuffer[hdrTotalLen] == 'G')
				&& (myAppBuffer[hdrTotalLen + 1U] == 'E')
				&& (myAppBuffer[hdrTotalLen + 2U] == 'T')) {
			const char *req = (const char*) &myAppBuffer[hdrTotalLen];
			uint16 respLen;

			/* The request text is used with strstr(), so it has to be
			 * terminated. DataLen is clamped above to keep this slot inside
			 * the buffer. */
			myAppBuffer[hdrTotalLen + tcpDataLen] = '\0';

			s_RemoteSeq = remoteSeq + tcpDataLen;

			/*
			 * Pick the response based on the requested command.  The command
			 * routes use fixed strings; the root URL gets a page generated at
			 * request time so it can carry the current temperature.
			 */
			if (strstr(req, "command1") != NULL) {
				static const char RESP[] =
						"HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nCommand1 executed";
				respLen = (uint16) (sizeof(RESP) - 1U);
				g_AppPendingCommand = 1U;
				(void) BuildAndSendTcp(CtrlIdx, TCP_PSH | TCP_ACK | TCP_FIN,
				ISN + 1UL, s_RemoteSeq, (const uint8*) RESP, respLen);
			} else if (strstr(req, "command2") != NULL) {
				static const char RESP[] =
						"HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nCommand2 executed";
				respLen = (uint16) (sizeof(RESP) - 1U);
				g_AppPendingCommand = 2U;
				(void) BuildAndSendTcp(CtrlIdx, TCP_PSH | TCP_ACK | TCP_FIN,
				ISN + 1UL, s_RemoteSeq, (const uint8*) RESP, respLen);
			} else if (strstr(req, "command3") != NULL) {
				static const char RESP[] =
						"HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nCommand3 executed";
				respLen = (uint16) (sizeof(RESP) - 1U);
				g_AppPendingCommand = 3U;
				(void) BuildAndSendTcp(CtrlIdx, TCP_PSH | TCP_ACK | TCP_FIN,
				ISN + 1UL, s_RemoteSeq, (const uint8*) RESP, respLen);
			} else {
				/* Root URL: serve the live temperature page. */
				const char *resp = BuildTemperaturePage(&respLen);
				(void) BuildAndSendTcp(CtrlIdx, TCP_PSH | TCP_ACK | TCP_FIN,
				ISN + 1UL, s_RemoteSeq, (const uint8*) resp, respLen);
			}
		}
		return;
	}
}
