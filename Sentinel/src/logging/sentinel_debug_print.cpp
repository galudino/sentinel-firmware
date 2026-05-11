///
/// \file       sentinel_debug_print.cpp
/// \brief      Debug Print Utilities - Implementation
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

#include "sentinel_debug_print.hpp"
#include "sentinel_format_string.hpp"
#include "sentinel_resource.hpp"

#include <cstdio>

namespace sentinel::logging {

static char
    g_log_buffer[DEBUG_OUTPUT_STREAM_MAX_LEN]; ///< Static buffer for formatting
                                               ///< log messages

} // namespace sentinel::logging

void sentinel::logging::bleprint_format(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    blevprint_format(fmt, args);
    va_end(args);
}

void sentinel::logging::blevprint_format(const char *fmt, va_list args) {
    char buf[128];

    auto len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len <= 0) {
        return;
    }

    // Clamp to buffer size
    if (static_cast<size_t>(len) >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }

    for (auto *it = buf; *it; it++) {
        g_ring_buffer.push(static_cast<uint8_t>(*it));
    }
}

void sentinel::logging::enqueue_log_for_debug_stream(const char *file, int line,
                                                     const char *function,
                                                     const char *level,
                                                     const char *fmt, ...) {
    // Use critical section to protect the static g_log_buffer
    taskENTER_CRITICAL();

    /// TODO: Initialize RTC and get real timestamp for log messages

    // uint64_t unix_ms = sensor.unix_time();
    uint64_t unix_ms = 0; // 1970-01-01T00:00:00Z in milliseconds - placeholder
                          // until RTC is implemented

    va_list args;
    va_start(args, fmt);

    auto length = sentinel::logging::build_string(
        g_log_buffer, sizeof(g_log_buffer), unix_ms, file, line, function,
        level, fmt, args);

    va_end(args);

    if (length > 0) {
        // Atomic write: check if the ENTIRE message fits before writing
        // any bytes. This prevents partial messages (which show as corrupt
        // unicode on the iOS app) from entering the ring buffer.
        if (static_cast<size_t>(length) <= g_ring_buffer.available_bytes()) {
            // Full message fits — write all bytes
            g_ring_buffer.push_bytes(
                reinterpret_cast<const uint8_t *>(g_log_buffer), length);
        }
        // else: drop entire message (better than corrupt partial message)
    }

    taskEXIT_CRITICAL();
}

using sentinel::logging::ring_buffer;

bool ring_buffer::push(uint8_t b) {
    taskENTER_CRITICAL();

    size_t next = (m_head + 1) % DEBUG_RING_BUFFER_CAPACITY;

    if (next == m_tail) {
        // Buffer full - drop the byte
        taskEXIT_CRITICAL();
        return false;
    }

    m_buffer[m_head] = b;
    m_head = next;

    taskEXIT_CRITICAL();
    return true;
}

bool ring_buffer::push_bytes(const uint8_t *data, size_t length) {
    bool all_pushed = true;

    for (auto i = size_t{}; i < length; i++) {
        m_buffer[m_head] = data[i];
        m_head = (m_head + 1) % DEBUG_RING_BUFFER_CAPACITY;
    }

    return all_pushed;
}

size_t ring_buffer::pop(uint8_t *out, size_t max_length) {
    taskENTER_CRITICAL();

    auto count = size_t{};

    while ((m_tail != m_head) && (count < max_length)) {
        out[count++] = m_buffer[m_tail];
        m_tail = (m_tail + 1) % DEBUG_RING_BUFFER_CAPACITY;
    }

    taskEXIT_CRITICAL();
    return count;
}

size_t ring_buffer::size() const {
    taskENTER_CRITICAL();

    auto size = size_t{};

    if (m_head >= m_tail) {
        size = m_head - m_tail;
    } else {
        size = DEBUG_RING_BUFFER_CAPACITY - m_tail + m_head;
    }

    taskEXIT_CRITICAL();
    return size;
}
