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

#ifndef P3T1750_H
#define P3T1750_H

/*==================================================================================================
 *                                        INCLUDE FILES
 ==================================================================================================*/
#include "CDD_I2c.h"

/*==================================================================================================
 *                                      DEFINES AND MACROS
 ==================================================================================================*/
#define TEMP_ADDR               0x48U   /* P3T1750 I2C slave address (7-bit; datasheet 0x90 is the 8-bit R/W address) */
#define TEMPERATURE_THRESHOLD   37U     /* Alert threshold in degrees Celsius */
#define MCU_TEMP_OFFSET 7.0f

/* I2c logical channel used as master. In this project channel 0 is the
 * master channel connected to the on-board temperature sensor. */
#ifndef I2C_MASTER_CHANNEL
#define I2C_MASTER_CHANNEL      0U
#endif

/* Software timeout used while polling the transfer status. */
#ifndef TIMEOUT
#define TIMEOUT                 0xFFFFFFUL
#endif

/*==================================================================================================
 *                                   FUNCTION PROTOTYPES
 ==================================================================================================*/

/**
 * @brief Read raw temperature data from P3T1750 sensor (2 bytes)
 * @param tempData  Pointer to buffer for raw temperature data
 * @return I2c channel status indicating success or failure
 */
I2c_StatusType ReadTemperature(uint8 *tempData);

/**
 * @brief Convert raw P3T1750 data to temperature in degrees Celsius
 * @param tempData  Pointer to raw temperature data (2 bytes)
 * @return Temperature value in degrees Celsius
 */
float ConvertTemperature_TMP102(uint8 *tempData);

#endif /* P3T1750_H */
