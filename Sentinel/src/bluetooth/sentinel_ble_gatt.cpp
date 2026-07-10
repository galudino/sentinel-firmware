///
/// \file    sentinel_ble_gatt.cpp
/// \brief   Bluetooth LE implementation for GATT operations
///
/// \details This source file provides the implementation for Bluetooth LE
///          functionality including GATT database operations and event
///          handling.
///
/// \author  galudino
/// \date    2021-2024
/// \version 1.0 - BLE module interface
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
#include "sentinel_device_context.hpp"
#include "sentinel_gatt_dis.hpp"
#include "sentinel_gatt_ds3231.hpp"
#include "sentinel_gatt_paged.hpp"
#include "sentinel_gatt_snapshot_stream.hpp"
#include "sentinel_gatt_system.hpp"
#include "sentinel_led_pwm.hpp"
#include "sentinel_task_ble_maintenance.hpp"
#include "sentinel_task_snapshot_stream.hpp"
#include "sentinel_task_battery_service.hpp"
#include "sentinel_task_debug_stream.hpp"
#include "sentinel_utilities.hpp"

#include <algorithm>
#include <cstring>

using sentinel::ble_context;

wiced_bt_gatt_status_t
sentinel::ble_gatt_db_set_value(uint16_t attr_handle, uint8_t *value,
                                uint16_t length) noexcept {
    auto status = wiced_bt_gatt_status_e::WICED_BT_GATT_INVALID_HANDLE;

    // Input guards (choose the status that matches your stack’s expectations)
    if (length > 0 && value == nullptr) {
        return wiced_bt_gatt_status_e::WICED_BT_GATT_INVALID_PDU;
    }

    for (auto i = uint16_t{}; i < app_gatt_db_ext_attr_tbl_size; i++) {
        auto &entry = app_gatt_db_ext_attr_tbl[i];

        if (entry.handle != attr_handle) {
            continue;
        }

        if (entry.max_len < length) {
            status = wiced_bt_gatt_status_e::WICED_BT_GATT_INVALID_ATTR_LEN;
            break;
        }

        if (entry.p_data == nullptr) {
            status = wiced_bt_gatt_status_e::
                WICED_BT_GATT_ERROR; // or INVALID_HANDLE if that’s your
                                     // convention
            break;
        }

        entry.cur_len = length;

        // If you require deterministic zeroed tail (good for BLE reads of
        // variable-length chars): Copy then zero the tail instead of blanking
        // the whole buffer.
        std::memcpy(entry.p_data, value, static_cast<size_t>(entry.cur_len));

        if (entry.max_len > entry.cur_len) {
            std::memset(entry.p_data + entry.cur_len, 0,
                        static_cast<size_t>(entry.max_len - entry.cur_len));
        }

        status = wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;

        if (entry.handle == HDLD_BAS_BATTERY_LEVEL_CLIENT_CHAR_CONFIG) {
            // Toggle notifications enabled/disabled here if you need to react
            // to CCCD changes.
        }

        break; // handled the matching handle; exit the loop
    }

    return to_underlying(status);
}

gatt_db_lookup_table_t *sentinel::ble_gatt_db_find_by_handle(uint16_t handle) {
    auto it =
        std::find_if(app_gatt_db_ext_attr_tbl,
                     app_gatt_db_ext_attr_tbl + app_gatt_db_ext_attr_tbl_size,
                     [handle](const gatt_db_lookup_table_t &entry) {
                         return entry.handle == handle;
                     });

    return (it != app_gatt_db_ext_attr_tbl + app_gatt_db_ext_attr_tbl_size)
               ? &(*it)
               : nullptr;
}

wiced_bt_gatt_status_t sentinel::ble_gatt_event_callback(
    wiced_bt_gatt_evt_t event,
    wiced_bt_gatt_event_data_t *event_data) noexcept {
    auto status = wiced_bt_gatt_status_t{};
    auto *attr_request = &event_data->attribute_request;
    auto error_handle = uint16_t{};

    using free_fn_t = void (*)(uint8_t *);

    switch (event) {
    case wiced_bt_gatt_evt_t::GATT_CONNECTION_STATUS_EVT:
        status = ble_context_object.connection_event_handler(
            &event_data->connection_status);
        break;

    case wiced_bt_gatt_evt_t::GATT_ATTRIBUTE_REQUEST_EVT:
        status = ble_gatt_event_handler(event_data, &error_handle);

        if (status != wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS) {
            wiced_bt_gatt_server_send_error_rsp(attr_request->conn_id,
                                                attr_request->opcode,
                                                error_handle, status);
        }

        break;

    case wiced_bt_gatt_evt_t::GATT_GET_RESPONSE_BUFFER_EVT:
        event_data->buffer_request.buffer.p_app_rsp_buffer =
            static_cast<uint8_t *>(
                std::malloc(event_data->buffer_request.len_requested));

        event_data->buffer_request.buffer.p_app_ctxt =
            reinterpret_cast<void *>(std::free);

        status = wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;

        break;

    case wiced_bt_gatt_evt_t::GATT_APP_BUFFER_TRANSMITTED_EVT: {
        auto free_fn =
            reinterpret_cast<free_fn_t>(event_data->buffer_xmitted.p_app_ctxt);

        if (free_fn) {
            free_fn(event_data->buffer_xmitted.p_app_data);
        }

        status = wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;
    } break;

    default:
        status = wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;
        break;
    }

    return status;
}

wiced_bt_gatt_status_t
sentinel::ble_gatt_event_handler(wiced_bt_gatt_event_data_t *event_data,
                                 uint16_t *error_handle) noexcept {
    auto status = wiced_bt_gatt_status_t{};
    auto *attr_request = &event_data->attribute_request;

    switch (attr_request->opcode) {
    case wiced_bt_gatt_opcode_e::GATT_REQ_READ:
    case wiced_bt_gatt_opcode_e::GATT_REQ_READ_BLOB:
        status = ble_gatt_request_read_handler(
            attr_request->conn_id, attr_request->opcode,
            &attr_request->data.read_req, attr_request->len_requested,
            error_handle);
        break;

    case wiced_bt_gatt_opcode_e::GATT_REQ_READ_BY_TYPE:
        status = ble_gatt_request_read_by_type_handler(
            attr_request->conn_id, attr_request->opcode,
            &attr_request->data.read_by_type, attr_request->len_requested,
            error_handle);
        break;

    case wiced_bt_gatt_opcode_e::GATT_REQ_READ_MULTI:
    case wiced_bt_gatt_opcode_e::GATT_REQ_READ_MULTI_VAR_LENGTH:
        status = ble_gatt_request_read_multi_handler(
            attr_request->conn_id, attr_request->opcode,
            &attr_request->data.read_multiple_req, attr_request->len_requested,
            error_handle);
        break;

    case wiced_bt_gatt_opcode_e::GATT_REQ_WRITE:
    case wiced_bt_gatt_opcode_e::GATT_CMD_WRITE:
    case wiced_bt_gatt_opcode_e::GATT_CMD_SIGNED_WRITE:
        status =
            sentinel::ble_gatt_command_write_handler(event_data, error_handle);

        if ((attr_request->opcode == wiced_bt_gatt_opcode_e::GATT_REQ_WRITE) &&
            (status == wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS)) {
            auto *p_write_request = &attr_request->data.write_req;
            wiced_bt_gatt_server_send_write_rsp(attr_request->conn_id,
                                                attr_request->opcode,
                                                p_write_request->handle);
        }

        break;

    case wiced_bt_gatt_opcode_e::GATT_REQ_PREPARE_WRITE:
        status = wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;
        break;

    case wiced_bt_gatt_opcode_e::GATT_REQ_EXECUTE_WRITE:
        wiced_bt_gatt_server_send_execute_write_rsp(attr_request->conn_id,
                                                    attr_request->opcode);
        status = wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;
        break;

    case wiced_bt_gatt_opcode_e::GATT_REQ_MTU: {
        auto local_mtu = wiced_bt_cfg_settings.p_ble_cfg->ble_max_rx_pdu_size;
        auto remote_mtu = attr_request->data.remote_mtu;

        status = wiced_bt_gatt_server_send_mtu_rsp(attr_request->conn_id,
                                                   remote_mtu, local_mtu);

        // Negotiated MTU is the minimum of local and remote
        auto negotiated_mtu = static_cast<uint16_t>(
            (remote_mtu < local_mtu) ? remote_mtu : local_mtu);

        // Update debug stream so notifications use the full MTU payload
        sentinel::ble_context_object.set_mtu(negotiated_mtu);
    } break;

    case wiced_bt_gatt_opcode_e::GATT_HANDLE_VALUE_CONF:
        sentinel::ble_context_object.ota_agent_confirmation_handler();
        status = wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;
        break;

    case wiced_bt_gatt_opcode_e::GATT_HANDLE_VALUE_NOTIF:
        status = wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;
        break;

    default:
        break;
    }

    return status;
}

wiced_bt_gatt_status_t sentinel::ble_gatt_request_read_handler(
    uint16_t connection_id, wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_read_t *read_request, uint16_t length_requested,
    uint16_t *error_handle) noexcept {
    auto *attribute = static_cast<gatt_db_lookup_table_t *>(nullptr);
    auto attr_length_to_copy = uint16_t{};
    auto length_to_send = uint16_t{};
    auto *attribute_data = static_cast<uint8_t *>(nullptr);

    *error_handle = read_request->handle;

    // Refresh paged Snapshot History / System Event Log values from their record
    // stores just before responding (#6). A no-op for every other handle.
    sentinel::gatt::paged::before_read(read_request->handle);

    if ((attribute = ble_gatt_db_find_by_handle(read_request->handle)) ==
        nullptr) {
        return wiced_bt_gatt_status_e::WICED_BT_GATT_INVALID_HANDLE;
    }

    attr_length_to_copy = attribute->cur_len;

    if (read_request->offset >= attribute->cur_len) {
        return wiced_bt_gatt_status_e::WICED_BT_GATT_INVALID_OFFSET;
    }

    length_to_send =
        MIN(length_requested, attr_length_to_copy - read_request->offset);
    attribute_data = attribute->p_data + read_request->offset;

    return wiced_bt_gatt_server_send_read_handle_rsp(
        connection_id, opcode, length_to_send, attribute_data, nullptr);
}

wiced_bt_gatt_status_t sentinel::ble_gatt_request_read_by_type_handler(
    uint16_t connection_id, wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_read_by_type_t *read_request, uint16_t length_requested,
    uint16_t *error_handle) noexcept {
    gatt_db_lookup_table_t *attribute = nullptr;

    auto last_handle = uint16_t{};
    auto attr_handle = read_request->s_handle;

    auto *response = static_cast<uint8_t *>(std::malloc(length_requested));
    auto pair_length = uint8_t{};

    auto used = 0;

    unused(last_handle);

    if (response == nullptr) {
        return wiced_bt_gatt_status_e::WICED_BT_GATT_INSUF_RESOURCE;
    }

    while (true) {
        *error_handle = attr_handle;
        last_handle = attr_handle;

        attr_handle = wiced_bt_gatt_find_handle_by_type(
            attr_handle, read_request->e_handle, &read_request->uuid);

        if (attr_handle == 0) {
            break;
        }

        if ((attribute = ble_gatt_db_find_by_handle(attr_handle)) == nullptr) {
            std::free(response);
            return wiced_bt_gatt_status_e::WICED_BT_GATT_INVALID_HANDLE;
        }

        {
            auto filled = wiced_bt_gatt_put_read_by_type_rsp_in_stream(
                response + used, length_requested - used, &pair_length,
                attr_handle, attribute->cur_len, attribute->p_data);

            if (filled == 0) {
                break;
            }

            used += filled;
        }

        ++attr_handle;
    }

    if (used == 0) {
        std::free(response);
        return wiced_bt_gatt_status_e::WICED_BT_GATT_INVALID_HANDLE;
    }

    wiced_bt_gatt_server_send_read_by_type_rsp(
        connection_id, opcode, pair_length, used, response,
        reinterpret_cast<void *>(std::free));

    return wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;
}

wiced_bt_gatt_status_t sentinel::ble_gatt_request_read_multi_handler(
    uint16_t connection_id, wiced_bt_gatt_opcode_t opcode,
    wiced_bt_gatt_read_multiple_req_t *read_multiple_request,
    uint16_t length_requested, uint16_t *error_handle) noexcept {
    auto *attribute = static_cast<gatt_db_lookup_table_t *>(nullptr);

    auto *response = static_cast<uint8_t *>(std::malloc(length_requested));

    auto handle = wiced_bt_gatt_get_handle_from_stream(
        read_multiple_request->p_handle_stream, 0);

    auto used = 0;

    *error_handle = handle;

    if (response == nullptr) {
        return wiced_bt_gatt_status_e::WICED_BT_GATT_INVALID_HANDLE;
    }

    for (auto i = 0; i < read_multiple_request->num_handles; i++) {
        handle = wiced_bt_gatt_get_handle_from_stream(
            read_multiple_request->p_handle_stream, i);
        *error_handle = handle;

        if ((attribute = ble_gatt_db_find_by_handle(handle)) == nullptr) {
            std::free(response);
            return wiced_bt_gatt_status_e::WICED_BT_GATT_ERR_UNLIKELY;
        }

        {
            auto filled = wiced_bt_gatt_put_read_multi_rsp_in_stream(
                opcode, response + used, length_requested - used,
                attribute->handle, attribute->cur_len, attribute->p_data);

            if (!filled) {
                break;
            }

            used += filled;
        }
    }

    if (used == 0) {
        return wiced_bt_gatt_status_e::WICED_BT_GATT_INVALID_HANDLE;
    }

    wiced_bt_gatt_server_send_read_multiple_rsp(
        connection_id, opcode, used, response,
        reinterpret_cast<void *>(std::free));

    return wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS;
}

wiced_bt_gatt_status_t
sentinel::ble_gatt_command_write_handler(wiced_bt_gatt_event_data_t *event_data,
                                         uint16_t *error_handle) noexcept {
    auto *write_request = &event_data->attribute_request.data.write_req;

    *error_handle = write_request->handle;

    CY_ASSERT((event_data != nullptr) && (write_request != nullptr));

    switch (write_request->handle) {
    case HDLD_OTA_FW_UPGRADE_SERVICE_OTA_UPGRADE_CONTROL_POINT_CLIENT_CHAR_CONFIG:
    case HDLC_OTA_FW_UPGRADE_SERVICE_OTA_UPGRADE_CONTROL_POINT_VALUE:
    case HDLC_OTA_FW_UPGRADE_SERVICE_OTA_UPGRADE_DATA_VALUE:
        return ble_context_object.ota_agent_write_handler(event_data,
                                                          error_handle);

    case HDLC_SYSTEM_SERIAL_NUMBER_VALUE: {
        // Store the new serial, then mirror it into the DIS Serial Number
        // display string (#45) so both stay in sync.
        auto status = ble_gatt_db_set_value(
            write_request->handle, write_request->p_val, write_request->val_len);
        if (status == wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS) {
            sentinel::gatt::dis::set_serial_number(
                sentinel::gatt::system::serial_number());
        }
        return status;
    }

    case HDLC_SYSTEM_REQUEST_BOOTLOADER_MODE_VALUE: {
        // Enter the bootloader for OTA DFU (#6): store the write, then request a
        // deferred reset from the maintenance task (never reset in the BT
        // callback — the write response must flush first).
        auto status = ble_gatt_db_set_value(
            write_request->handle, write_request->p_val, write_request->val_len);
        if (status == wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS) {
            sentinel::task::ble_maintenance_task::instance().request_bootloader();
        }
        return status;
    }

    case HDLC_SNAPSHOT_HISTORY_CLEAR_STORE_VALUE: {
        // Erase the snapshot history store (async — erase_all is multi-second).
        auto status = ble_gatt_db_set_value(
            write_request->handle, write_request->p_val, write_request->val_len);
        if (status == wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS) {
            sentinel::task::ble_maintenance_task::instance()
                .request_clear_snapshots();
        }
        return status;
    }

    case HDLC_SYSTEM_EVENT_LOG_CLEAR_STORE_VALUE: {
        // Erase the event log store (async — erase_all is multi-second).
        auto status = ble_gatt_db_set_value(
            write_request->handle, write_request->p_val, write_request->val_len);
        if (status == wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS) {
            sentinel::task::ble_maintenance_task::instance()
                .request_clear_events();
        }
        return status;
    }

    case HDLC_SNAPSHOT_STREAM_SNAPSHOT_NOTIFY_ENABLE_VALUE: {
        // Drive the live snapshot stream task (#46, lane 2) from the enable
        // characteristic (#6): 1 => start streaming, 0 => return to idle.
        auto status = ble_gatt_db_set_value(
            write_request->handle, write_request->p_val, write_request->val_len);
        if (status == wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS) {
            if (sentinel::gatt::snapshot_stream::enable_flag()) {
                sentinel::task::snapshot_stream_task::instance().start();
            } else {
                sentinel::task::snapshot_stream_task::instance().stop();
            }
        }
        return status;
    }

    case HDLC_DS3231_UNIX_TIME_VALUE: {
        // BLE time-sync (#6): store the written epoch, then push it to the
        // DS3231 over the arbitrated I²C bus. This is the *only* time-set path;
        // rtc_service stays read-only (RTC time design, decision recorded in
        // project memory). Writes are rare + user-initiated, so doing the bus
        // transaction here (serialized by the I²C arbiter) is acceptable.
        auto status = ble_gatt_db_set_value(
            write_request->handle, write_request->p_val, write_request->val_len);
        if (status == wiced_bt_gatt_status_e::WICED_BT_GATT_SUCCESS &&
            sentinel::resource::context_ready()) {
            sentinel::resource::context().rtc.set_unix_time(
                sentinel::gatt::ds3231::unix_time());
        }
        return status;
    }

    default:
        return ble_gatt_db_set_value(write_request->handle,
                                     write_request->p_val,
                                     write_request->val_len);
    }
}
