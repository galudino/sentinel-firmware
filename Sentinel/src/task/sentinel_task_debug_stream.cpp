///
/// \file       sentinel_debug_stream_task.cpp
/// \brief      BLE Debug Output Stream - Implementation
///
/// Implements a ring buffer-based debug output system that transmits log
/// messages over BLE notifications to a connected iOS client application.
///
/// \author     galudino
/// \date       2026-05-15
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cycfg_gatt_db.h"
#include "cyhal.h"

#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_ble_context.hpp"
#include "sentinel_debug_print.hpp"
#include "sentinel_format_string.hpp"
#include "sentinel_gatt_debug.hpp"
#include "sentinel_resource.hpp"
#include "sentinel_task_debug_stream.hpp"
#include "sentinel_utilities.hpp"

#include <cstdio>
#include <cstring>

namespace {

//==============================================================================
// BLE Notification Helpers
//==============================================================================

///
/// \brief      Send a notification on the Output Stream characteristic.
///
/// \param      data    Data to send
/// \param      length  Length of data
/// \return     BLE API result
///
wiced_bt_gatt_status_t send_notification_for_output_stream(const uint8_t *data,
                                                           size_t length);

//==============================================================================
// Notification Pump
//==============================================================================

///
/// \brief      Drain the ring buffer and send notifications.
///
/// Called periodically by the DebugStreamTask.
/// Checks all gates before sending:
/// - BLE connected
/// - CCCD enabled (client subscribed)
/// - persistent.debugNotifyStreamEnable set
///
void output_stream_notifier(void);

} // namespace

using namespace sentinel::task;

//==============================================================================
// Public API Implementation
//==============================================================================

debug_stream &debug_stream::instance() noexcept {
    static debug_stream stream;
    return stream;
}

BaseType_t debug_stream::task_create() noexcept {
    return xTaskCreate(&debug_stream::task_trampoline, "Debug Stream Task",
                       (configMINIMAL_STACK_SIZE * 4), this, 1, &m_handle);
}

void debug_stream::task_trampoline(void *args) {
    static_cast<debug_stream *>(args)->run();
}

void debug_stream::run() {
    /// TODO: Wait for BLE task to be ready

    /// TODO: Wait for Persistent Task to be ready

    // Send a startup message to confirm the pipeline is working
    logi("Debug Stream Task started - BLE debug output ready");

    // NOTE: Do NOT force-enable here.
    // The client app controls this by writing 1 (enable) or 0 (disable)
    // to the Output Stream Notify Enable characteristic.

    // Main loop: drain buffer and send notifications at ~50Hz
    while (true) {
        // Gate 1: BLE must be connected
        if (!ble_context_object.connected()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Gate 2: Client must have subscribed to Output Stream (CCCD)
        if (!sentinel::gatt::debug::output_stream_notifications_enabled()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Gate 3: Client must have written 1 to Output Stream Notify Enable
        if (!sentinel::gatt::debug::output_notify_stream_enabled()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        output_stream_notifier();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

bool debug_stream::is_enabled() noexcept {
    if (!ble_context_object.connected()) {
        return false;
    }

    if (!sentinel::gatt::debug::output_stream_notifications_enabled()) {
        return false;
    }

    if (!sentinel::gatt::debug::output_notify_stream_enabled()) {
        return false;
    }

    return true;
}

//==============================================================================
// Module-private function implementations
//==============================================================================

namespace {

wiced_bt_gatt_status_t send_notification_for_output_stream(const uint8_t *data,
                                                           size_t length) {
    return wiced_bt_gatt_server_send_notification(
        sentinel::ble_context_object.connection_id(),
        HDLC_DEBUG_OUTPUT_STREAM_VALUE, static_cast<uint16_t>(length),
        const_cast<uint8_t *>(data), nullptr);
}

void output_stream_notifier(void) {
    using sentinel::ble_context_object;

    // One notification carries at most ATT_MTU - 3 bytes, further bounded by
    // the GATT characteristic length. The negotiated MTU governs; a message
    // longer than this budget is truncation-marked by pop_frame (fragmentation
    // is deferred to the client mirror, #34).
    size_t max_payload =
        (ble_context_object.mtu() > 3) ? (ble_context_object.mtu() - 3) : 20;
    if (max_payload > app_debug_output_stream_len) {
        max_payload = app_debug_output_stream_len;
    }

    // Pop exactly one '\0'-terminated frame (one log line) so the client
    // decodes each notification as a single C-string log entry.
    uint8_t tx_buffer[sentinel::logging::DEBUG_OUTPUT_STREAM_MAX_LEN];
    auto to_send =
        sentinel::logging::g_ring_buffer.pop_frame(tx_buffer, max_payload);

    if (to_send == 0) {
        return;
    }

    // Send notification
    auto status = send_notification_for_output_stream(tx_buffer, to_send);

    if (!status) {
        // Best-effort: try to push data back (may drop if full)
        // This is optional - we could just drop on failure
        // For now, we'll just accept the loss
    }
}

} // namespace
