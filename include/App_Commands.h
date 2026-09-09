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
 * @file    App_Commands.h
 * @brief   Shared command mailbox between the HTTP server and the application.
 *
 * Http_Server.c sets g_AppPendingCommand to 1, 2, or 3 when the matching URL
 * command is received.  main.c polls the variable, executes the thermostat
 * action, and clears it back to 0.
 *
 * Values:
 *   0 - no pending command
 *   1 - command1: increase temperature setpoint
 *   2 - command2: decrease temperature setpoint
 *   3 - command3: reset temperature setpoint to default (24 deg C)
 */

#ifndef APP_COMMANDS_H
#define APP_COMMANDS_H

#include "Std_Types.h"

/** @brief Command mailbox written by Http_Server.c, read and cleared by main.c. */
extern volatile uint8 g_AppPendingCommand;

#endif /* APP_COMMANDS_H */
