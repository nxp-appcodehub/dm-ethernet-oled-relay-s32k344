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
 * @file    Http_Server.h
 * @brief   Public interface of the lightweight HTTP/TCP server for S32K344.
 *
 * This module implements a minimal, single-connection HTTP/1.0 server on top
 * of a bare-metal TCP state machine.  It is designed to be polled from the
 * main loop via the Ethernet stack (Eth_Stack.c / EthIf_RxIndication).
 *
 * Supported subset of TCP/HTTP:
 *   - Three-way handshake (SYN -> SYN-ACK -> ACK).
 *   - Single HTTP GET request -> 200 OK response with configurable HTML body.
 *   - Connection teardown initiated by the server (FIN piggybacked on the
 *     HTTP response segment).
 *   - RST handling: resets the server back to LISTEN state immediately.
 *
 * Usage:
 *   1. Call Http_SetTemperature() whenever a new sensor reading is available,
 *      so the page served to browsers reflects the latest value.
 *   2. Call Http_HandleTcp() from the IPv4 dispatcher whenever an IPv4 frame
 *      with proto = 6 (TCP) is received.
 *
 * Network identity:
 *   The server listens on HTTP_SERVER_PORT (port 80) and uses the board IP
 *   address defined inside Http_Server.c.  Both values must match the
 *   configuration in Eth_Stack.c.
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "Eth_43_GMAC.h"

/**
 * @brief   TCP port the HTTP server listens on.
 *
 * Standard HTTP port 80.  Must match the port used by the browser client.
 * Also used as the TCP source port in all server-to-client segments.
 */
#define HTTP_SERVER_PORT    (80U)

/**
 * @brief   Process an incoming TCP segment directed at the HTTP server.
 *
 * Called from the IPv4 dispatcher in Eth_Stack.c whenever a TCP frame
 * (EtherType 0x0800, IP proto 6) is received.  The function implements the
 * minimal TCP state machine required to accept one HTTP GET connection at a
 * time and return a complete HTTP/1.0 response.
 *
 * Internally the function:
 *   - Handles SYN (new connection): sends SYN-ACK and advances to SYN_RCVD.
 *   - Handles the final ACK of the three-way handshake: advances to ESTABLISHED.
 *   - Handles PSH+data (HTTP request): if the first three bytes are "GET",
 *     builds and transmits the HTTP 200 response with the HTML body, then
 *     sends FIN to close the connection.
 *   - Handles FIN (client close): sends ACK and returns to LISTEN.
 *   - Handles RST: immediately returns to LISTEN.
 *
 * Only one connection is tracked at a time.  A new SYN overwrites any
 * previously cached peer state.
 *
 * @param[in]  CtrlIdx   GMAC controller index (0).
 * @param[in]  DataPtr   Pointer to the start of the IPv4 datagram
 *                       (IP header + TCP header + TCP data).
 * @param[in]  DataLen   Total length of the IPv4 datagram in bytes.
 * @param[in]  PeerMac   Pointer to the 6-byte Ethernet source MAC address of
 *                       the sender, required to address the TCP reply frame.
 */
void Http_HandleTcp(uint8 CtrlIdx, const Eth_DataType *DataPtr, uint16 DataLen,
		const uint8 *PeerMac);

/**
 * @brief   Publish the latest temperature reading to the web server.
 *
 * The value is cached internally and rendered into the HTML page whenever a
 * browser requests the root URL.  The page is built at request time from this
 * cached value, so no reading is taken on the network path: a blocking LPI2C
 * transfer inside the TCP handler would stall the GMAC polling loop and could
 * drop frames.
 *
 * Until this function is called for the first time the page reports the
 * reading as unavailable rather than showing a misleading zero.
 *
 * @param[in]  RawCode  Signed 12-bit temperature code from the P3T1750DP,
 *                      in units of 0.0625 degC per LSB.
 */
void Http_SetTemperature(sint16 RawCode);

#endif /* HTTP_SERVER_H */
