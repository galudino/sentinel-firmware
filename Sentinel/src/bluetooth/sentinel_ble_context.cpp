///
/// \file    sentinel_ble_context.cpp
/// \brief   Bluetooth LE common implementation
///
/// \details This file implements common Bluetooth LE functionality shared
///          between OTA and non-OTA builds.
///
/// \author  galudino
/// \date    2021-2024
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

#include "sentinel_ble_context.hpp"
#include "sentinel_ble_gatt.hpp"
#include "sentinel_cyhal_pwm_signal.hpp"
#include "sentinel_device_context.hpp"
#include "sentinel_firmware_version.hpp"
#include "sentinel_led_pwm.hpp"
#include "sentinel_pwm_signal.hpp"
#include "sentinel_resource.hpp"
#include "sentinel_task_battery_service.hpp"
#include "sentinel_utilities.hpp"

#include <algorithm>
#include <cstring>

using sentinel::ble_context;

/// \brief Local alias for the PWM signal type driving the advertising LED.
using Signal = sentinel::cyhal_pwm_signal;

/// \brief PWM signal bound to LED3, used to indicate advertising/connection
///        state (see \ref sentinel::ble_context::update_advertising_led).
static auto led3_pwm_block = Signal(&sentinel::resource::led3);

namespace sentinel {

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

} // namespace sentinel

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

namespace {
/// \brief Reason written into \c firmware_update_record.mcuboot_result for a
///        \c firmware_update_failed event (#83); mirrored by the client Logs
///        view.
constexpr uint32_t OTA_FAIL_ABORTED = 1u;     ///< client sent the ABORT command
constexpr uint32_t OTA_FAIL_SIGNAL_LOST = 2u; ///< peer disconnected mid-OTA
constexpr uint32_t OTA_FAIL_VERIFY = 3u;      ///< image verification failed
} // namespace

wiced_bt_gatt_status_t ble_context::connection_event_handler(
    const wiced_bt_gatt_connection_status_t &connection_status) {
    auto status = wiced_bt_gatt_status_e::WICED_BT_GATT_ERROR;
    auto result = wiced_result_t::WICED_BT_ERROR;

    if (connection_status.connected) {
        m_connection_id = connection_status.conn_id;

        std::copy_n(connection_status.bd_addr, BD_ADDR_LEN,
                    m_peer_address.begin());

        m_connection_state = state::connected;

        // #76: ask the central for a 15 ms connection interval. Paged reads
        // and OTA are request/response bound (every Index write and block read
        // waits out connection events), so the central's default 30 ms+
        // interval dominated a whole-store walk. 15 ms min == max is within
        // Apple's accessory guidelines; the central may still refuse.
        wiced_bt_l2cap_update_ble_conn_params(
            m_peer_address.data(), PREFERRED_CONN_INTERVAL,
            PREFERRED_CONN_INTERVAL, PREFERRED_CONN_LATENCY,
            PREFERRED_SUPERVISION_TIMEOUT);

        // #80: log the connection. conn_interval / MTU are not yet negotiated
        // at this point, so they are left 0 (unknown) for now.
        if (sentinel::resource::context_ready()) {
            sentinel::resource::context().event_log().record_ble_connected(
                connection_status.bd_addr, connection_status.addr_type, 0u, 0u);
        }
    } else {
        m_connection_id = 0;

        result = wiced_bt_start_advertisements(
            wiced_bt_ble_advert_mode_e::BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0,
            nullptr);

        if (result != wiced_result_t::WICED_BT_SUCCESS) {
            CY_ASSERT(false);
        }

        m_connection_state = state::disconnected_and_advertising;

        // #80: log the disconnect with the HCI/host reason code.
        if (sentinel::resource::context_ready()) {
            sentinel::resource::context().event_log().record_ble_disconnected(
                connection_status.bd_addr, connection_status.addr_type,
                static_cast<uint8_t>(connection_status.reason));
        }

        // #83: a disconnect while an OTA was in flight (signal loss) leaves the
        // agent half-alive — log the failure (reason = signal-lost) and tear it
        // down so the device returns to a clean state and a retry starts fresh.
        if (m_ota_context != nullptr) {
            if (sentinel::resource::context_ready()) {
                sentinel::resource::context()
                    .event_log()
                    .record_firmware_update(
                        sentinel::diagnostics::system_event::
                            firmware_update_failed,
                        sentinel::current_firmware_version,
                        sentinel::firmware_version(0u, 0u, 0u, 0u),
                        OTA_FAIL_SIGNAL_LOST);
            }
            ota_cleanup();
        }
    }

    update_advertising_led();
    status = wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;

    return status;
}

cy_rslt_t ble_context::update_advertising_led() noexcept {
    using FrontLED = sentinel::led_pwm<Signal>;
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

    auto front_led = FrontLED(led3_pwm_block);
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

    // #83: a prior OTA that was aborted or interrupted (signal loss) may have
    // left the agent running; tear it down first so cy_ota_agent_start does not
    // fail with "OTA context already created!".
    ota_cleanup();

    ota_value_initialize();

    result = cy_ota_agent_start(&m_ota_network_params, &m_ota_agent_params,
                                &m_ota_context);

    if (result != CY_RSLT_SUCCESS) {
        // #83: a failed start must leave the device alive and retryable — do
        // not hang here (this was an infinite while(true) delay loop).
        m_ota_context = nullptr;
    }

    return result;
}

void ble_context::ota_cleanup() noexcept {
    // #83: stop the OTA agent and drop the context so a later PREPARE can start
    // a fresh one. No-op when no OTA is active.
    if (m_ota_context != nullptr) {
        cy_ota_agent_stop(&m_ota_context);
        m_ota_context = nullptr;
    }
}

wiced_bt_gatt_status_t
ble_context::ota_agent_write_handler(wiced_bt_gatt_event_data_t *event_data,
                                     uint16_t &error_handle) noexcept {
    auto *write_request = &event_data->attribute_request.data.write_req;

    cy_rslt_t result = cy_en_rslt_type_t::CY_RSLT_TYPE_ERROR;

    auto gatt_status = wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;

    error_handle = write_request->handle;

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

            // #80: an OTA download is now starting. The target version is not
            // known until the image header arrives, so `to` is left 0.0.0.0.
            // (firmware_update_completed / _reverted need a boot-time swap
            // detector — staged as a follow-up.)
            if (sentinel::resource::context_ready()) {
                sentinel::resource::context()
                    .event_log()
                    .record_firmware_update(
                        sentinel::diagnostics::system_event::
                            firmware_update_attempted,
                        sentinel::current_firmware_version,
                        sentinel::firmware_version(0u, 0u, 0u, 0u), 0u);
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
                // #80/#83: image verification failed — the swap will not
                // happen. Log it (reason = verify-failed) and tear the agent
                // down so a retry starts clean.
                if (sentinel::resource::context_ready()) {
                    sentinel::resource::context()
                        .event_log()
                        .record_firmware_update(
                            sentinel::diagnostics::system_event::
                                firmware_update_failed,
                            sentinel::current_firmware_version,
                            sentinel::firmware_version(0u, 0u, 0u, 0u),
                            OTA_FAIL_VERIFY);
                }
                ota_cleanup();
                return wiced_bt_gatt_status_e::WICED_BT_GATT_ERROR;
            }

            return gatt_status;

        case CY_OTA_UPGRADE_COMMAND_ABORT:
            result = cy_ota_ble_download_abort(m_ota_context);

            // #80/#83: the client aborted the OTA. Log it (reason = aborted)
            // and tear the agent down so a retry starts clean.
            if (sentinel::resource::context_ready()) {
                sentinel::resource::context()
                    .event_log()
                    .record_firmware_update(
                        sentinel::diagnostics::system_event::
                            firmware_update_failed,
                        sentinel::current_firmware_version,
                        sentinel::firmware_version(0u, 0u, 0u, 0u),
                        OTA_FAIL_ABORTED);
            }
            ota_cleanup();

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
        ota_cleanup(); // #83: stop + null the context (was stop-only)
    }
}

namespace {

/// \brief ~1 s throttle between link-metric HCI reads.
constexpr uint32_t LINK_METRICS_MIN_INTERVAL_MS = 1000;

/// \brief read-RSSI completion: cache the peer RSSI on the shared context.
/// \param p_data Callback payload; expected to be a
///               \c wiced_bt_dev_rssi_result_t*.
void rssi_read_complete(void *p_data) {
    auto *result = static_cast<wiced_bt_dev_rssi_result_t *>(p_data);
    if (result != nullptr &&
        result->status == wiced_result_t::WICED_BT_SUCCESS) {
        sentinel::ble_context_object.set_peer_rssi(result->rssi);
    }
}

/// \brief read-TX-power completion: cache the connection TX power.
/// \param p_data Callback payload; expected to be a
///               \c wiced_bt_tx_power_result_t*.
void tx_power_read_complete(void *p_data) {
    auto *result = static_cast<wiced_bt_tx_power_result_t *>(p_data);
    if (result != nullptr &&
        result->status == wiced_result_t::WICED_BT_SUCCESS) {
        sentinel::ble_context_object.set_tx_power_dbm(result->tx_power);
    }
}

} // namespace

void ble_context::refresh_link_metrics() noexcept {
    if (!connected()) {
        return;
    }

    // Throttle: the snapshot populate path may call this every ~100 ms, but two
    // HCI reads per second is plenty for a health metric.
    const auto now_ms =
        static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
    if ((now_ms - m_last_metrics_tick) < LINK_METRICS_MIN_INTERVAL_MS) {
        return;
    }
    m_last_metrics_tick = now_ms;

    // Non-blocking: results arrive later via the completion callbacks.
    wiced_bt_dev_read_rssi(m_peer_address.data(), BT_TRANSPORT_LE,
                           &rssi_read_complete);
    wiced_bt_dev_read_tx_power(m_peer_address.data(), BT_TRANSPORT_LE,
                               &tx_power_read_complete);
}

wiced_bt_dev_status_t ble_context::stack_management_callback(
    wiced_bt_management_evt_t event,
    wiced_bt_management_evt_data_t *event_data) noexcept {
    auto result = wiced_result_t::WICED_BT_ERROR;
    wiced_bt_device_address_t device_address{};
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

            // #80: log that the BLE stack came up. If the event log is not yet
            // bound (BTM_ENABLED can precede the store bring-up), this no-ops.
            if (sentinel::resource::context_ready()) {
                sentinel::resource::context()
                    .event_log()
                    .record_ble_stack_initialized();
            }
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
        unused(encryption_status);
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
        ble_context_object.set_advertising_mode(
            event_data->ble_advert_state_changed);
        ble_context_object.update_advertising_led();

        result = wiced_result_t::WICED_BT_SUCCESS;
        break;

    default:
        break;
    }

    return result;
}

static wiced_bt_gatt_status_t sentinel::ble_start_advertising() {
    auto gatt_status = wiced_bt_gatt_status_t{};
    auto wiced_result = wiced_result_t::WICED_BT_ERROR;

    wiced_bt_set_pairable_mode(true, 0);
    wiced_bt_ble_set_raw_advertisement_data(CY_BT_ADV_PACKET_DATA_SIZE,
                                            cy_bt_adv_packet_data);

    // #67: advertise the System 128-bit service UUID in the primary packet so a
    // service-filtered iOS scan can discover the board; carry the Complete
    // Local Name ("Sentinel") in the scan response, which iOS active-scans and
    // merges.
    wiced_bt_ble_set_raw_scan_response_data(CY_BT_SCAN_RESP_PACKET_DATA_SIZE,
                                            cy_bt_scan_resp_packet_data);

    gatt_status = wiced_bt_gatt_register(sentinel::ble_gatt_event_callback);
    gatt_status =
        wiced_bt_gatt_db_init(gatt_database, gatt_database_len, nullptr);

    // Record the GATT-DB registration result for the POST gatt_db probe (#6).
    sentinel::ble_context_object.set_gatt_db_ok(
        gatt_status == wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS);

    wiced_result = wiced_bt_start_advertisements(
        wiced_bt_ble_advert_mode_e::BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, nullptr);

    if (wiced_result != wiced_result_t::WICED_BT_SUCCESS) {
        CY_ASSERT(false);
    }

    return gatt_status;
}
