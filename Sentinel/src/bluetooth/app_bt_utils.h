///
/// \file   app_bt_utils.h
/// \brief  This file consists of the utility functions that will help
///         debugging and developing the applications easier with much
///         more meaningful information.
///
/// \see    README.md
///

/******************************************************************************
 * Copyright 2021-2024, Cypress Semiconductor Corporation (an Infineon company)
 * or an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
 *
 * This software, including source code, documentation and related
 * materials ("Software") is owned by Cypress Semiconductor Corporation
 * or one of its affiliates ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products.  Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 *******************************************************************************/

#ifndef APP_BT_UTILS_H
#define APP_BT_UTILS_H

#include "wiced_bt_dev.h"
#include "wiced_bt_gatt.h"

#include <stdio.h>

#define CASE_RETURN_STR(enum_val)                                              \
    case enum_val:                                                             \
        return #enum_val;

#define FROM_BIT16_TO_8(val) ((uint8_t)((val) >> 8))

///
/// \brief Prints the address of the bluetooth device
///
/// \param wiced_bt_device_address_t bdadr bluetooth device address
///
void print_bd_address(wiced_bt_device_address_t bdadr);

///
/// \brief Converts the wiced_bt_management_evt_t enum value to
/// its corresponding string literal.
///
/// \param wiced_bt_management_evt_t event Bluetooth event type
///
/// \return string representation of wiced_bt_management_evt_t value
///
const char *get_bt_event_name(wiced_bt_management_evt_t event);

///
/// \brief Converts the wiced_bt_ble_advert_mode_t enum value to
/// its corresponding string literal.
///
/// \param wiced_bt_ble_advert_mode_t mode Bluetooth advertising mode type
///
/// \return string representation of wiced_bt_ble_advert_mode_t value
///
const char *get_bt_advert_mode_name(wiced_bt_ble_advert_mode_t mode);

///
/// \brief Converts the wiced_bt_gatt_disconn_reason_t enum value to
/// its corresponding string literal.
///
/// \param wiced_bt_ble_advert_mode_t reason Bluetooth disconnection reason
///
/// \return string representation of wiced_bt_gatt_disconn_reason_t
///
const char *
get_bt_gatt_disconn_reason_name(wiced_bt_gatt_disconn_reason_t reason);

///
/// \brief Converts the wiced_bt_gatt_status_t enum value to
/// its corresponding string literal.
///
/// \param wiced_bt_ble_advert_mode_t status Bluetooth GATT status
///
/// \return string representation of wiced_bt_gatt_status_t
///
const char *get_bt_gatt_status_name(wiced_bt_gatt_status_t status);

#endif /* APP_BT_UTILS_H */
