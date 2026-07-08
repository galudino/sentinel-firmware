///
/// \file    sentinel_task_snapshot_stream.cpp
/// \brief   Live device_snapshot BLE stream service task implementation (#46)
///
/// \details Implements the snapshot stream task declared in
///          \c sentinel_task_snapshot_stream.hpp. The task is normally idle:
///          it blocks on a task notification with zero CPU until \ref start
///          wakes it, then loops \c populate_snapshot() → notify sink at the
///          configured cadence while a capture session is active \b and a
///          central is connected, auto-returning to idle on \ref stop or
///          disconnect.
///
///          \c populate_snapshot() is cache-backed (decision #14): it reads the
///          BME280 sample cache (#37), the rtc_service caches, and FreeRTOS
///          uptime — never a fresh bus transaction — so this 100 ms loop adds
///          \b no I²C/SPI contention. The actual GATT notification is #6's job;
///          here we only call the attached notify sink.
///
/// \author  galudino
/// \date    2026-06-29
/// \version 1.0 - Initial live snapshot stream task implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_log.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_ble_context.hpp"
#include "sentinel_debug_print.hpp"
#include "sentinel_device_snapshot.hpp"
#include "sentinel_task_snapshot_stream.hpp"

#include <cstdint>

namespace {

///
/// \brief Floor on the cadence. The stream is gated by notify throughput, not
///        by data freshness (the caches refresh at ~1 Hz, decision #14), so a
///        period below this only burns CPU resending near-duplicate frames.
///
constexpr uint32_t MIN_PERIOD_MS = 20;

///
/// \brief Default connection predicate: query the live BLE context.
///
/// \details Used until #6 (or the testbench) overrides it. A snapshot stream is
///          never notified into the void — if no central is connected, the loop
///          falls back to idle.
///
bool default_connected() noexcept {
    return sentinel::ble_context_object.connected();
}

} // namespace

using namespace sentinel::task;

snapshot_stream_task &snapshot_stream_task::instance() noexcept {
    static snapshot_stream_task task;
    return task;
}

bool snapshot_stream_task::central_connected() const noexcept {
    return m_connected != nullptr ? m_connected() : default_connected();
}

void snapshot_stream_task::start() noexcept {
    m_streaming = true;
    // Wake the task if it is blocked in the idle wait. Harmless if it is
    // already streaming — the run loop re-checks m_streaming, not the
    // notification.
    if (m_handle != nullptr) {
        xTaskNotifyGive(m_handle);
    }
}

void snapshot_stream_task::stop() noexcept { m_streaming = false; }

bool snapshot_stream_task::streaming() const noexcept { return m_streaming; }

void snapshot_stream_task::set_period_ms(uint32_t period_ms) noexcept {
    m_period_ms = period_ms < MIN_PERIOD_MS ? MIN_PERIOD_MS : period_ms;
}

uint32_t snapshot_stream_task::period_ms() const noexcept {
    return m_period_ms;
}

void snapshot_stream_task::set_notify_sink(notify_fn sink) noexcept {
    m_notify_sink = sink;
}

void snapshot_stream_task::set_connected_predicate(
    connected_fn predicate) noexcept {
    m_connected = predicate;
}

BaseType_t snapshot_stream_task::task_create(UBaseType_t priority,
                                             uint16_t stack_words,
                                             uint32_t period_ms) noexcept {
    set_period_ms(period_ms);
    return xTaskCreate(&snapshot_stream_task::task_trampoline,
                       "Snapshot Stream Task", stack_words, this, priority,
                       &m_handle);
}

void snapshot_stream_task::task_trampoline(void *task_parameter) {
    static_cast<snapshot_stream_task *>(task_parameter)->run();
}

void snapshot_stream_task::run() {
    logi("snapshot_stream: idle (awaiting capture session)", "");
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "Snapshot stream: idle (awaiting capture session)\n");

    while (true) {
        // ---- Idle: block with zero CPU until start() wakes us. ----
        // The notification is only a wake signal; m_streaming is the source of
        // truth, so a missed/spurious notification just re-blocks here.
        while (!m_streaming) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        logi("snapshot_stream: streaming at %d ms",
             static_cast<int>(m_period_ms));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "Snapshot stream: streaming started\n");

        // ---- Stream: notify at the cadence while enabled AND connected. ----
        while (m_streaming && central_connected()) {
            auto snapshot = sentinel::telemetry::device_snapshot::make();

            if (m_notify_sink != nullptr) {
                m_notify_sink(snapshot);
            }

            vTaskDelay(pdMS_TO_TICKS(m_period_ms));
        }

        // ---- Auto-stop: a disconnect mid-stream clears the session so the
        // task never resumes notifying a central that has gone away. A plain
        // stop() already cleared the flag; this makes disconnect equivalent.
        m_streaming = false;

        logi("snapshot_stream: returned to idle", "");
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "Snapshot stream: returned to idle\n");
    }
}
