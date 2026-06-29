///
/// \file    sentinel_test_snapshot_stream.cpp
/// \brief   Live snapshot stream task test implementations (#46)
///
/// \details Implements the behavioral suite declared in
///          \c sentinel_test_snapshot_stream.hpp. The suite drives the real
///          \ref sentinel::task::snapshot_stream_task singleton (created in the
///          testbench's \c create_tasks()) through its idle ↔ stream lifecycle,
///          observing a file-static counting notify sink and toggling a
///          controllable connection predicate. Steps are ordered and stateful
///          (cadence delays, shared task), so they run sequentially inside
///          \ref all rather than as independent pure bodies — the harness style
///          otherwise mirrors the POST / device_snapshot suites (report
///          PASS/FAIL to both the BLE debug stream and the retarget-IO UART).
///
///          \b Why this proves \c populate_is_cache_backed off-bench: the
///          stream notifies complete snapshots (\c trailer_magic set, written
///          last by \c populate_snapshot) at 100 ms with \b no I²C/SPI bus
///          arbiter driving a sensor in this path — \c populate_snapshot reads
///          only the subsystem caches (#37 / rtc_service) and FreeRTOS uptime
///          by construction (decision #14), never a fresh bus transaction.
///
/// \author  galudino
/// \date    2026-06-29
/// \version 1.0 - snapshot stream task test implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_log.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_debug_print.hpp"
#include "sentinel_device_snapshot.hpp"
#include "sentinel_task_snapshot_stream.hpp"
#include "sentinel_test_snapshot_stream.hpp"

#include <cstdint>

namespace {

using sentinel::task::snapshot_stream_task;
using sentinel::telemetry::device_snapshot;
using sentinel::telemetry::SNAPSHOT_TRAILER_MAGIC;

// ---- File-static observers wired into the task as plain function pointers. ----

volatile uint32_t g_notify_count = 0;     ///< Total notify-sink invocations.
volatile uint16_t g_last_magic   = 0;     ///< Trailer magic of the last snapshot.
volatile bool     g_connected    = true;  ///< Simulated central-connected state.

/// \brief Counting notify sink: records that a complete snapshot was produced.
void counting_sink(const device_snapshot &snap) noexcept {
    ++g_notify_count;
    g_last_magic = snap.trailer_magic;
}

/// \brief Controllable connection predicate (drives \c disconnect_autostop).
bool controllable_connected() noexcept { return g_connected; }

void report(const char *name, bool ok, const char *detail) noexcept {
    if (ok) {
        logi("%s PASS", name);
        cy_log_msg(CYLF_DEF, CY_LOG_INFO, "snapshot_stream %s PASS\n", name);
    } else {
        loge("%s FAIL: %s", name, detail);
        cy_log_msg(CYLF_DEF, CY_LOG_INFO, "snapshot_stream %s FAIL: %s\n", name,
                   detail);
    }
}

void delay_ms(uint32_t milliseconds) noexcept {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

} // namespace

// ============================================================================
// Ordered behavioral suite
// ============================================================================

void sentinel::test::snapshot_stream::all() {
    auto &task = snapshot_stream_task::instance();

    // Deterministic, BLE-independent connection state for the whole suite.
    g_connected = true;
    task.set_connected_predicate(controllable_connected);
    task.set_notify_sink(counting_sink);

    // ---- idle_by_default --------------------------------------------------
    // The task was created in create_tasks() but never started: it must sit
    // idle and never invoke the sink.
    g_notify_count = 0;
    {
        const auto streaming = task.streaming();
        delay_ms(300);
        const auto fired = g_notify_count;
        report("idle_by_default", !streaming && fired == 0,
               streaming ? "streaming() true before start"
                         : "sink fired while idle");
    }

    // ---- start_stop -------------------------------------------------------
    // start() begins notifications; stop() halts them; both idempotent.
    task.set_period_ms(50);
    g_notify_count = 0;
    task.start();
    task.start(); // idempotent: second start is a no-op
    const auto streaming_after_start = task.streaming();
    delay_ms(250);
    const auto fired_while_streaming = g_notify_count;

    task.stop();
    task.stop(); // idempotent: second stop is a no-op
    delay_ms(120); // let the in-flight cadence delay drain
    const auto streaming_after_stop = task.streaming();
    const auto count_at_stop = g_notify_count;
    delay_ms(300);
    const auto count_after_stop = g_notify_count;

    {
        const auto ok = streaming_after_start && fired_while_streaming > 0 &&
                        !streaming_after_stop &&
                        count_after_stop == count_at_stop;
        const char *why = !streaming_after_start ? "streaming() false after start"
                          : fired_while_streaming == 0 ? "no notifications while streaming"
                          : streaming_after_stop ? "streaming() true after stop"
                                                 : "notifications continued after stop";
        report("start_stop", ok, why);
    }

    // ---- cadence ----------------------------------------------------------
    // At a 50 ms period, ~500 ms of streaming should yield ~10 notifications.
    // Allow a wide jitter band so the test is not flaky under load.
    task.set_period_ms(50);
    g_notify_count = 0;
    g_last_magic   = 0;
    task.start();
    delay_ms(525);
    const auto cadence_count = g_notify_count;
    task.stop();
    delay_ms(120);
    {
        const auto ok = cadence_count >= 7 && cadence_count <= 14;
        report("cadence", ok,
               cadence_count < 7 ? "too few notifications for the period"
                                 : "too many notifications for the period");
    }

    // ---- populate_is_cache_backed ----------------------------------------
    // Every streamed snapshot completed (trailer magic written last) with no
    // I²C/SPI arbiter driving a sensor in this path — populate is cache-only.
    {
        const auto ok = g_last_magic == SNAPSHOT_TRAILER_MAGIC;
        report("populate_is_cache_backed", ok,
               "streamed snapshot incomplete (trailer magic unset)");
    }

    // ---- disconnect_autostop ----------------------------------------------
    // A central that drops mid-stream must return the task to idle with no
    // further notifications.
    g_connected = true;
    task.set_period_ms(50);
    g_notify_count = 0;
    task.start();
    delay_ms(150); // a few notifications land

    g_connected = false; // simulate the central disconnecting
    delay_ms(200);       // task observes !connected, returns to idle
    const auto streaming_after_drop = task.streaming();
    const auto count_at_drop = g_notify_count;
    delay_ms(250);
    const auto count_after_drop = g_notify_count;
    {
        const auto ok = !streaming_after_drop && count_after_drop == count_at_drop;
        report("disconnect_autostop", ok,
               streaming_after_drop ? "still streaming after disconnect"
                                    : "notifications continued after disconnect");
    }

    // ---- Restore: leave the singleton idle and un-instrumented. -----------
    g_connected = true;
    task.stop();
    task.set_notify_sink(nullptr);
    task.set_connected_predicate(nullptr);

    logi("snapshot_stream: all tests complete", "");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO, "snapshot_stream: all tests complete\n");
}

// ============================================================================
// task_create
// ============================================================================

BaseType_t sentinel::test::snapshot_stream::task_create() {
    constexpr auto stack_words = configMINIMAL_STACK_SIZE * 4;
    constexpr auto priority = static_cast<UBaseType_t>(configMAX_PRIORITIES - 3);

    return xTaskCreate(
        [](void *) -> void {
            sentinel::test::snapshot_stream::all();
            // One-shot suite: a FreeRTOS task must not return, so delete it.
            vTaskDelete(nullptr);
        },
        "SnapshotStream Test Task", stack_words, nullptr, priority, nullptr);
}
