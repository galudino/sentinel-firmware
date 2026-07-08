///
/// \file       sentinel_log.cpp
/// \brief      Unified logging facade - Implementation
///
/// \author     galudino
/// \date       2026-07-08
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cyhal.h"

#include "FreeRTOS.h"
#include "portmacro.h"
#include "semphr.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_debug_print.hpp"
#include "sentinel_format_string.hpp"
#include "sentinel_log.hpp"
#include "sentinel_log_sink.hpp"
#include "sentinel_task_rtc_service.hpp"

#include <cstdio>

namespace sentinel::logging {

//==============================================================================
// Module-private state
//==============================================================================

/// Static staging buffer the line is formatted into (once) before fan-out.
/// Protected by \ref g_log_mutex; kept off the caller's stack to spare tasks
/// with 200-300 word stacks.
static char g_log_buffer[DEBUG_OUTPUT_STREAM_MAX_LEN];

/// Serializes producers and guards \ref g_log_buffer. Created by \ref init.
/// Held across the (blocking) serial write, mirroring the former cy_log mutex.
static SemaphoreHandle_t g_log_mutex = nullptr;

/// Monotonic per-line sequence index. Shared by both sinks so a gap on serial
/// and a gap on BLE line up; wraps at 16 bits (also the fragment msg_id, #34).
static uint16_t g_seq = 0;

} // namespace sentinel::logging

//==============================================================================
// Public API
//==============================================================================

void sentinel::logging::init() noexcept {
    if (g_log_mutex == nullptr) {
        g_log_mutex = xSemaphoreCreateMutex();
    }
}

const char *sentinel::logging::to_string(level l) noexcept {
    switch (l) {
    case level::debug:
        return "debug";
    case level::info:
        return "info";
    case level::warn:
        return "warn";
    case level::error:
        return "error";
    }

    return "info";
}

void sentinel::logging::log(level l, const char *file, int line,
                            const char *function, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(l, file, line, function, fmt, args);
    va_end(args);
}

void sentinel::logging::vlog(level l, const char *file, int line,
                             const char *function, const char *fmt,
                             va_list args) {
    // Take the mutex only once the scheduler is running; the earliest log calls
    // (boot banner) run single-threaded before vTaskStartScheduler().
    const bool use_lock = g_log_mutex != nullptr &&
                          xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;

    if (use_lock) {
        xSemaphoreTake(g_log_mutex, portMAX_DELAY);
    }

    // Real wall-clock timestamp from the DS3231, via the rtc_service cache
    // (#5/#38). last_unix_time() is a lock-free read of a latched 32-bit value,
    // returning 0 until the first 1 Hz SQW tick (rendered as the epoch).
    const uint64_t unix_ms =
        static_cast<uint64_t>(
            sentinel::task::rtc_service::instance().last_unix_time()) *
        1000ull;

    const uint16_t seq = g_seq++;

    const auto length = sentinel::logging::build_string(
        g_log_buffer, sizeof(g_log_buffer), seq, unix_ms, file, line, function,
        to_string(l), fmt, args);

    if (length > 0) {
        const auto len = static_cast<size_t>(length);
        sink::serial::write(l, g_log_buffer, len);
        sink::ble_debug::write(l, g_log_buffer, len);
    }

    if (use_lock) {
        xSemaphoreGive(g_log_mutex);
    }
}

//==============================================================================
// Sink backends
//==============================================================================

void sentinel::logging::sink::serial::write(level l, const char *line,
                                            size_t len) noexcept {
    static_cast<void>(len);

    // Gate: debug stays off the serial wire, matching the former
    // cy_log(CY_LOG_INFO) behaviour.
    if (l == level::debug) {
        return;
    }

    // build_string emits no terminator; serial lines end in '\n'.
    std::printf("%s\n", line);
}

void sentinel::logging::sink::ble_debug::write(level l, const char *line,
                                               size_t len) noexcept {
    static_cast<void>(l);

    // Frame the line for the datagram client: <line>'\0'. The trailing null
    // terminates the C-string the client decodes, and delimits one message for
    // the drain (one notification per frame, #34).
    //
    // Atomic-vs-drain: keep the admission check and the push in one critical
    // section so the drain task cannot interleave. All-or-nothing — only write
    // if the ENTIRE frame (line + null) fits, so a partial (corrupt) entry
    // never enters the ring.
    taskENTER_CRITICAL();

    if (len + 1 <= g_ring_buffer.available_bytes()) {
        g_ring_buffer.push_bytes(reinterpret_cast<const uint8_t *>(line), len);
        const uint8_t nul = 0;
        g_ring_buffer.push_bytes(&nul, 1);
    }

    taskEXIT_CRITICAL();
}
