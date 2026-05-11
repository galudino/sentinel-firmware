///
/// \file    ble_context.cpp
/// \brief   Bluetooth LE common implementation
///
/// \details This file implements common Bluetooth LE functionality shared
///          between OTA and non-OTA builds.
///
/// \author  galudino
/// \date    2025
/// \version 1.0 - BLE module common implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cycfg_bt_settings.h"
#include "cycfg_gap.h"
#include "cycfg_gatt_db.h"
#include "cycfg_peripherals.h"

#include "cy_ota_api.h"
#include "cy_result.h"

#include "cyabs_rtos.h"

#include "cybsp.h"
#include "cybsp_bt_config.h"

#include "cybt_platform_trace.h"

#include "cyhal.h"
#include "cyhal_gpio.h"
#include "cyhal_pwm.h"
#include "cyhal_wdt.h"

#include "wiced_bt_ble.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_l2c.h"
#include "wiced_bt_stack.h"
#include "wiced_bt_uuid.h"
#include "wiced_memory.h"
#include "wiced_result.h"

#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

#include "battery_service_task.hpp"
#include "ble_context.hpp"
#include "ble_gatt.hpp"
#include "cyhal_pwm_signal.hpp"
#include "led_pwm.hpp"
#include "pwm_signal.hpp"
#include "resource.hpp"
#include "utilities.hpp"

#include <algorithm>
#include <cstring>

using Signal = cyhal_pwm_signal;

static auto led_pwm_block = Signal(&resource::led3);

///
/// \brief Initialize and start BLE advertising
///
/// Initializes the advertising LED PWM, enables pairable mode, configures
/// advertisement packet data, registers the GATT event callback, initializes
/// the GATT database, and starts undirected high-duty cycle advertising.
/// Triggers assertions on failure of critical operations.
///
/// \return wiced_bt_gatt_status_t GATT status from database initialization,
///         typically WICED_BT_GATT_SUCCESS. Critical failures trigger
///         assertions.
///
static wiced_bt_gatt_status_t ble_start_advertising();

wiced_result_t ble_context::stack_initialize() noexcept {
    default_value_initialize();

    cybt_platform_config_init(&cybsp_bt_platform_cfg);

    auto result =
        wiced_bt_stack_init(stack_management_callback, &wiced_bt_cfg_settings);

    if (result != wiced_result_t::WICED_BT_SUCCESS) {
        CY_ASSERT(false);
    }

    return result;
}

wiced_bt_gatt_status_t ble_context::connection_event_handler(
    wiced_bt_gatt_connection_status_t *connection_status) {
    auto status = wiced_bt_gatt_status_e::WICED_BT_GATT_ERROR;
    auto result = wiced_result_t::WICED_BT_ERROR;

    if (connection_status == nullptr) {
        return status;
    }

    if (connection_status->connected) {
        m_connection_id = connection_status->conn_id;

        std::copy_n(connection_status->bd_addr, BD_ADDR_LEN,
                    m_peer_address.begin());

        m_connection_state = state::connected;
    } else {
        m_connection_id = 0;

        result = wiced_bt_start_advertisements(
            wiced_bt_ble_advert_mode_e::BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0,
            nullptr);

        if (result != wiced_result_t::WICED_BT_SUCCESS) {
            CY_ASSERT(false);
        }

        m_connection_state = state::disconnected_and_advertising;
    }

    update_advertising_led();
    status = wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;

    return status;
}

cy_rslt_t ble_context::update_advertising_led() noexcept {
    using FrontLED = led_pwm<Signal>;
    using DutyCycle = FrontLED::duty_cycle;

    auto duty_cycle = DutyCycle::off;

    switch (m_connection_state) {
    case state::disconnected_not_advertising:
        duty_cycle = DutyCycle::off;
        break;
    case state::disconnected_and_advertising:
        duty_cycle = DutyCycle::blinking;
        break;
    case state::connected:
        duty_cycle = DutyCycle::on;
        break;
    default:
        duty_cycle = DutyCycle::off;
        break;
    }

    auto front_led = FrontLED(led_pwm_block);
    auto result = cy_rslt_t{};

    result = front_led.stop();
    result = front_led.set_blink_rate(duty_cycle);
    result = front_led.start();

    return result;
}

cy_rslt_t ble_context::ota_agent_initialize() noexcept {
    cy_rslt_t result = cy_en_rslt_type_t::CY_RSLT_TYPE_ERROR;

    if (m_tag != OTA_APP_TAG_VALID) {
        return CY_RSLT_OTA_ERROR_BADARG;
    }

    ota_value_initialize();

    result = cy_ota_agent_start(&m_ota_network_params, &m_ota_agent_params,
                                &m_ota_context);

    if (result != CY_RSLT_SUCCESS) {
        while (true) {
            cy_rtos_delay_milliseconds(10);
        }
    }

    return result;
}

wiced_bt_gatt_status_t
ble_context::ota_agent_write_handler(wiced_bt_gatt_event_data_t *event_data,
                                     uint16_t *error_handle) noexcept {
    auto *write_request = &event_data->attribute_request.data.write_req;

    cy_rslt_t result = cy_en_rslt_type_t::CY_RSLT_TYPE_ERROR;

    auto gatt_status = wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;

    *error_handle = write_request->handle;

    CY_ASSERT((event_data != nullptr) && (write_request != nullptr));

    switch (write_request->handle) {
    case HDLD_OTA_FW_UPGRADE_SERVICE_OTA_UPGRADE_CONTROL_POINT_CLIENT_CHAR_CONFIG:
        // Save configuration descriptor (Notify & Indicate flags)
        m_ota_config_descriptor = write_request->p_val[0];

        return wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;

    case HDLC_OTA_FW_UPGRADE_SERVICE_OTA_UPGRADE_CONTROL_POINT_VALUE:
        switch (write_request->p_val[0]) {
        case CY_OTA_UPGRADE_COMMAND_PREPARE_DOWNLOAD:
            // Call application-level OTA initialization
            result = ota_agent_initialize();

            if (result != CY_RSLT_SUCCESS) {
                return wiced_bt_gatt_status_e::WICED_BT_GATT_ERROR;
            }

            result = cy_ota_ble_download_prepare(m_ota_context, m_connection_id,
                                                 m_ota_config_descriptor);

            if (result != CY_RSLT_SUCCESS) {
                return wiced_bt_gatt_status_e::WICED_BT_GATT_ERROR;
            }

            return wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;

        case CY_OTA_UPGRADE_COMMAND_DOWNLOAD:
            // Let OTA library know download is starting
            result =
                cy_ota_ble_download(m_ota_context, event_data, m_connection_id,
                                    m_ota_config_descriptor);

            if (result != CY_RSLT_SUCCESS) {
                return wiced_bt_gatt_status_e::WICED_BT_GATT_ERROR;
            }

            return wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;

        case CY_OTA_UPGRADE_COMMAND_VERIFY:
            result = cy_ota_ble_download_verify(m_ota_context, event_data,
                                                m_connection_id);

            if (result != CY_RSLT_SUCCESS) {
                return wiced_bt_gatt_status_e::WICED_BT_GATT_ERROR;
            }

            return gatt_status;

        case CY_OTA_UPGRADE_COMMAND_ABORT:
            result = cy_ota_ble_download_abort(m_ota_context);

            return wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;
        }

        break;

    case HDLC_OTA_FW_UPGRADE_SERVICE_OTA_UPGRADE_DATA_VALUE:
        result = cy_ota_ble_download_write(m_ota_context, event_data);
        return (result == CY_RSLT_SUCCESS)
                   ? wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS
                   : wiced_bt_gatt_status_e::WICED_BT_GATT_ERROR;

    default:
        break;
    }

    return wiced_bt_gatt_status_e::WICED_BT_GATT_REQ_NOT_SUPPORTED;
}

void ble_context::ota_agent_confirmation_handler() noexcept {
    auto ota_lib_state = cy_ota_agent_state_t::CY_OTA_STATE_NOT_INITIALIZED;
    cy_ota_get_state(m_ota_context, &ota_lib_state);

    if (ota_lib_state == cy_ota_agent_state_t::CY_OTA_STATE_OTA_COMPLETE &&
        m_reboot_at_end) {
        cy_rtos_delay_milliseconds(1000);
        NVIC_SystemReset();
    } else {
        cy_ota_agent_stop(&m_ota_context);
    }
}

wiced_bt_dev_status_t ble_context::stack_management_callback(
    wiced_bt_management_evt_t event,
    wiced_bt_management_evt_data_t *event_data) noexcept {
    auto result = wiced_result_t::WICED_BT_ERROR;
    wiced_bt_device_address_t device_address{};
    wiced_bt_ble_advert_mode_t *advertisement_mode = nullptr;
    wiced_bt_dev_encryption_status_t *encryption_status = nullptr;

    switch (event) {
    case wiced_bt_management_evt_e::BTM_ENABLED_EVT:
        if (event_data->enabled.status == wiced_result_t::WICED_BT_SUCCESS) {
            wiced_bt_set_local_bdaddr(
                static_cast<uint8_t *>(cy_bt_device_address), BLE_ADDR_PUBLIC);
            wiced_bt_dev_read_local_addr(device_address);

            auto gatt_status = ble_start_advertising();

            if (gatt_status != wiced_result_t::WICED_BT_SUCCESS) {
                CY_ASSERT(false);
            }

            result = wiced_result_t::WICED_BT_SUCCESS;
        }
        break;

    case wiced_bt_management_evt_e::BTM_USER_CONFIRMATION_REQUEST_EVT:
        wiced_bt_dev_confirm_req_reply(
            wiced_result_t::WICED_BT_SUCCESS,
            event_data->user_confirmation_request.bd_addr);
        result = wiced_result_t::WICED_BT_SUCCESS;
        break;

    case wiced_bt_management_evt_e::BTM_PASSKEY_NOTIFICATION_EVT:
        result = wiced_result_t::WICED_BT_SUCCESS;
        break;

    case wiced_bt_management_evt_e::BTM_PAIRING_IO_CAPABILITIES_BLE_REQUEST_EVT:
        event_data->pairing_io_capabilities_ble_request.local_io_cap =
            wiced_bt_dev_io_cap_e::BTM_IO_CAPABILITIES_NONE;
        event_data->pairing_io_capabilities_ble_request.oob_data =
            wiced_bt_dev_oob_data_e::BTM_OOB_NONE;
        event_data->pairing_io_capabilities_ble_request.auth_req =
            wiced_bt_dev_le_auth_req_e::BTM_LE_AUTH_REQ_BOND |
            wiced_bt_dev_le_auth_req_e::BTM_LE_AUTH_REQ_MITM;
        event_data->pairing_io_capabilities_ble_request.max_key_size = 0x10;
        event_data->pairing_io_capabilities_ble_request.init_keys =
            wiced_bt_dev_le_key_type_e::BTM_LE_KEY_PENC |
            wiced_bt_dev_le_key_type_e::BTM_LE_KEY_PID;
        event_data->pairing_io_capabilities_ble_request.resp_keys =
            wiced_bt_dev_le_key_type_e::BTM_LE_KEY_PENC |
            wiced_bt_dev_le_key_type_e::BTM_LE_KEY_PID;

        result = wiced_result_t::WICED_BT_SUCCESS;
        break;

    case wiced_bt_management_evt_e::BTM_PAIRING_COMPLETE_EVT:
        result = wiced_result_t::WICED_BT_SUCCESS;
        break;

    case wiced_bt_management_evt_e::BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT:
        result = wiced_result_t::WICED_BT_SUCCESS;
        break;

    case wiced_bt_management_evt_e::BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT:
        result = wiced_result_t::WICED_BT_ERROR;
        break;

    case wiced_bt_management_evt_e::BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT:
        result = wiced_result_t::WICED_BT_SUCCESS;
        break;

    case wiced_bt_management_evt_e::BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT:
        result = wiced_result_t::WICED_BT_ERROR;
        break;

    case wiced_bt_management_evt_e::BTM_ENCRYPTION_STATUS_EVT:
        encryption_status = &event_data->encryption_status;
        util::unused(encryption_status);
        result = wiced_result_t::WICED_BT_SUCCESS;
        break;

    case wiced_bt_management_evt_e::BTM_SECURITY_REQUEST_EVT:
        wiced_bt_ble_security_grant(event_data->security_request.bd_addr,
                                    wiced_result_t::WICED_BT_SUCCESS);
        result = wiced_result_t::WICED_BT_SUCCESS;
        break;

    case wiced_bt_management_evt_e::BTM_BLE_CONNECTION_PARAM_UPDATE:
        result = wiced_result_t::WICED_BT_SUCCESS;
        break;

    case wiced_bt_management_evt_e::BTM_BLE_ADVERT_STATE_CHANGED_EVT:
        advertisement_mode = &event_data->ble_advert_state_changed;

        ble_context_object.set_advertising_mode(advertisement_mode);
        ble_context_object.update_advertising_led();

        result = wiced_result_t::WICED_BT_SUCCESS;
        break;

    default:
        break;
    }

    return result;
}

static wiced_bt_gatt_status_t ble_start_advertising() {
    auto gatt_status = wiced_bt_gatt_status_t{};
    auto wiced_result = wiced_result_t::WICED_BT_ERROR;

    wiced_bt_set_pairable_mode(true, 0);
    wiced_bt_ble_set_raw_advertisement_data(CY_BT_ADV_PACKET_DATA_SIZE,
                                            cy_bt_adv_packet_data);

    gatt_status = wiced_bt_gatt_register(ble_gatt_event_callback);
    gatt_status =
        wiced_bt_gatt_db_init(gatt_database, gatt_database_len, nullptr);

    wiced_result = wiced_bt_start_advertisements(
        wiced_bt_ble_advert_mode_e::BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, nullptr);

    if (wiced_result != wiced_result_t::WICED_BT_SUCCESS) {
        CY_ASSERT(false);
    }

    return gatt_status;
}
