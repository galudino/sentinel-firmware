///
/// \file       sentinel_debug_print.hpp
/// \brief      BLE debug-stream ring buffer + raw print - Header
///
/// \details    Owns the debug output ring buffer that the BLE debug stream task
///             drains, plus the raw \c bleprintf path. Structured, multi-sink
///             logging (\c logd/logi/logw/loge) now lives in the logging facade
///             \ref sentinel_log.hpp, which this header includes so existing
///             call sites keep compiling unchanged.
///
/// \author     galudino
/// \date       2026-05-15
///

#ifndef SENTINEL_DEBUG_PRINT_HPP
#define SENTINEL_DEBUG_PRINT_HPP

#include "sentinel_log.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace sentinel::logging {

///
/// \brief      Single-producer / single-consumer byte ring buffer backing the
///             BLE debug output stream.
///
/// \details    Producer: the logging facade's BLE sink. Consumer: the debug
///             stream task's drain. Best-effort — bytes are dropped when full.
///
class ring_buffer {
public:
    ///
    /// \brief      Construct an empty ring buffer.
    ///
    explicit ring_buffer() {}

    ///
    /// \brief      Get the free space currently available in the ring buffer.
    ///
    /// \return     Number of bytes that can still be pushed before it is full.
    ///
    size_t available_bytes() const {
        return m_head >= m_tail
                   ? DEBUG_RING_BUFFER_CAPACITY - 1 - m_head + m_tail
                   : m_tail - m_head - 1;
    }

    ///
    /// \brief      Push a single byte into the ring buffer.
    ///
    /// Thread-safe via FreeRTOS critical sections. If the buffer is full, the
    /// byte is dropped (best-effort).
    ///
    /// \param      b       Byte to push.
    /// \return     true if pushed, false if the buffer was full (dropped).
    ///
    bool push(uint8_t b);

    ///
    /// \brief      Push multiple bytes into the ring buffer.
    ///
    /// If the buffer is full, bytes are dropped (best-effort). Callers that
    /// require an intact write must pre-check \ref available_bytes.
    ///
    /// \param      data    Pointer to data to push.
    /// \param      length  Number of bytes to push.
    /// \return     true (write is unconditional; capacity is the caller's
    ///             responsibility).
    ///
    bool push_bytes(const uint8_t *data, size_t length);

    ///
    /// \brief      Pop multiple bytes from the ring buffer.
    ///
    /// Thread-safe via FreeRTOS critical sections.
    ///
    /// \param      out         Output buffer.
    /// \param      max_length  Maximum bytes to pop.
    /// \return     Number of bytes actually popped.
    ///
    size_t pop(uint8_t *out, size_t max_length);

    ///
    /// \brief      Pop one '\0'-delimited frame (a single log line).
    ///
    /// \details    Copies bytes up to (and consuming) the next '\0' delimiter
    ///             into \p out, then null-terminates \p out. If the frame is
    ///             longer than \p max_length, the excess is consumed from the
    ///             ring but dropped from \p out (single clean truncation). The
    ///             producer writes each frame atomically, so a complete '\0'
    ///             always terminates each queued frame.
    ///
    ///             Thread-safe via FreeRTOS critical sections.
    ///
    /// \param      out         Output buffer (always null-terminated on
    /// return).
    /// \param      max_length  Capacity of \p out including the null
    /// terminator.
    /// \return     Bytes written to \p out including the null terminator, or 0
    ///             if no complete frame is available.
    ///
    size_t pop_frame(uint8_t *out, size_t max_length);

    ///
    /// \brief      Get the number of bytes currently stored in the ring buffer.
    ///
    /// \return     Number of bytes available to pop.
    ///
    size_t size() const;

private:
    static constexpr size_t DEBUG_RING_BUFFER_CAPACITY = 2048;

    uint8_t m_buffer[DEBUG_RING_BUFFER_CAPACITY]; ///< Ring buffer storage.

    volatile size_t m_head = 0; ///< Write position.
    volatile size_t m_tail = 0; ///< Read position.
};

inline ring_buffer g_ring_buffer; ///< Ring buffer instance for debug output.

/// Max bytes of one rendered log line / one BLE notification payload. Matches
/// the Debug Output Stream GATT characteristic (design.cybt, 512). A single
/// notification is still bounded at runtime by min(ATT_MTU - 3, this).
static constexpr auto DEBUG_OUTPUT_STREAM_MAX_LEN = 512;

///
/// \brief      Write a formatted string directly to the debug ring buffer.
///
/// \details    printf-style, no metadata prefix. Best-effort: characters are
///             dropped if the buffer is full.
///
/// \param      fmt     Format string (printf-style).
/// \param      ...     Variable arguments.
///
void bleprint_format(const char *fmt, ...);

///
/// \brief      \c va_list form of \ref bleprint_format.
///
/// \param      fmt     Format string.
/// \param      args    va_list of arguments.
///
void blevprint_format(const char *fmt, va_list args);

} // namespace sentinel::logging

#if BLE_DEBUG_ENABLE

/// Raw debug printf — writes straight to the BLE debug stream, no metadata.
#define bleprintf(...) sentinel::logging::bleprint_format(__VA_ARGS__)

#else

#define bleprintf(...)                                                         \
    do {                                                                       \
    } while (0)

#endif /* BLE_DEBUG_ENABLE */

#endif /* SENTINEL_DEBUG_PRINT_HPP */
