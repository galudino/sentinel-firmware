///
/// \file       sentinel_debug_print.hpp
/// \brief      Debug Print Utilities - Header
///
/// \author     galudino
/// \date       2026-05-15
///

#ifndef SENTINEL_DEBUG_PRINT_HPP
#define SENTINEL_DEBUG_PRINT_HPP

/// Debug output stream enable/disable
/// Set to 0 to compile out all debug output
#define BLE_DEBUG_ENABLE 1

/// *** IMPORTANT: NEVER use %f, %e, %g (float format specifiers) in    ***
/// *** logi/logw/loge/logd calls! vsnprintf float-to-string conversion ***
/// *** uses 500-1500+ bytes of stack on ARM Cortex-M newlib, which     ***
/// *** overflows tasks with 200-300 word stacks and corrupts memory.   ***
/// *** Use integer casts instead: static_cast<int>(float_val)          ***
/// *** or static_cast<int>(float_val * N)                              ***

#include <cstdarg>
#include <cstddef>

namespace sentinel::logging {

class ring_buffer {
public:
    explicit ring_buffer() {}

    ///
    /// \brief      Get the number of bytes currently stored in the ring buffer.
    ///
    /// \return     Number of bytes in buffer
    ///
    size_t available_bytes() const {
        return m_head >= m_tail
                   ? DEBUG_RING_BUFFER_CAPACITY - 1 - m_head + m_tail
                   : m_tail - m_head - 1;
    }

    ///
    /// \brief      Push a single byte into the ring buffer.
    ///
    /// Thread-safe via FreeRTOS critical sections.
    /// If buffer is full, the byte is dropped (best-effort).
    ///
    /// \param      b       Byte to push
    /// \return     true if pushed, false if buffer was full (dropped)
    ///
    bool push(uint8_t b);

    ///
    /// \brief      Push multiple bytes into the ring buffer.
    ///
    /// Thread-safe via FreeRTOS critical sections.
    /// If buffer is full, bytes are dropped (best-effort).
    ///
    /// \param      data    Pointer to data to push
    /// \param      length  Number of bytes to push
    /// \return     true if all bytes pushed, false if buffer was full (some
    /// dropped)
    ///
    bool push_bytes(const uint8_t *data, size_t length);

    ///
    /// \brief      Pop multiple bytes from the ring buffer.
    ///
    /// Thread-safe via FreeRTOS critical sections.
    ///
    /// \param      out         Output buffer
    /// \param      max_length     Maximum bytes to pop
    /// \return     Number of bytes actually popped
    ///
    size_t pop(uint8_t *out, size_t max_length);

    ///
    /// \brief      Get the current size of data in the ring buffer.
    ///
    /// \return     Number of bytes in buffer
    ///
    size_t size() const;

private:
    static constexpr size_t DEBUG_RING_BUFFER_CAPACITY = 256;

    uint8_t m_buffer[DEBUG_RING_BUFFER_CAPACITY]; ///< Ring buffer storage

    volatile size_t m_head = 0; ///< Ring buffer head index (write position)
    volatile size_t m_tail = 0; ///< Ring buffer tail index (read position)
};

inline ring_buffer g_ring_buffer; ///< Ring buffer instance for debug output

static constexpr auto DEBUG_OUTPUT_STREAM_MAX_LEN = 128;

///
/// \brief      Write a formatted string to the debug ring buffer.
///
/// printf-style formatting. If the buffer is full, characters are dropped
/// (best-effort semantics).
///
/// \param      fmt     Format string (printf-style)
/// \param      ...     Variable arguments
///
void bleprint_format(const char *fmt, ...);

///
/// \brief      Write a formatted string to the debug ring buffer (va_list
/// version).
///
/// \param      fmt     Format string
/// \param      args    va_list of arguments
///
void blevprint_format(const char *fmt, va_list args);

///
/// \brief      Enqueue a structured log message to the debug stream.
///
/// Uses a static internal buffer protected by critical sections to
/// minimize stack impact on the calling task.
///
/// \param      file        Source filename (__FILE__)
/// \param      line        Source line number (__LINE__)
/// \param      function    Function name (__func__)
/// \param      level       Log level string ("debug", "info", "warn", "error")
/// \param      fmt         printf-style format string
/// \param      ...         Variable arguments
///
void enqueue_log_for_debug_stream(const char *file, int line,
                                  const char *function, const char *level,
                                  const char *fmt, ...);

} // namespace sentinel::logging

#if BLE_DEBUG_ENABLE

/// Debug printf macro - outputs to BLE debug stream
#define bleprintf(fmt, ...)                                                    \
    sentinel::logging::bleprint_format(fmt, ##__VA_ARGS__)

/// Log debug level message
#define logd(fmt, ...)                                                         \
    sentinel::logging::enqueue_log_for_debug_stream(                           \
        __FILE__, __LINE__, __func__, "debug", fmt, ##__VA_ARGS__)

/// Log info level message
#define logi(fmt, ...)                                                         \
    sentinel::logging::enqueue_log_for_debug_stream(                           \
        __FILE__, __LINE__, __func__, "info", fmt, ##__VA_ARGS__)

/// Log warning level message
#define logw(fmt, ...)                                                         \
    sentinel::logging::enqueue_log_for_debug_stream(                           \
        __FILE__, __LINE__, __func__, "warn", fmt, ##__VA_ARGS__)

/// Log error level message
#define loge(fmt, ...)                                                         \
    sentinel::logging::enqueue_log_for_debug_stream(                           \
        __FILE__, __LINE__, __func__, "error", fmt, ##__VA_ARGS__)

#else

/// Debug printf macro - compiled out when disabled
#define bleprintf(fmt, ...)                                                    \
    do {                                                                       \
    } while (0)
#define logd(fmt, ...)                                                         \
    do {                                                                       \
    } while (0)
#define logi(fmt, ...)                                                         \
    do {                                                                       \
    } while (0)
#define logw(fmt, ...)                                                         \
    do {                                                                       \
    } while (0)
#define loge(fmt, ...)                                                         \
    do {                                                                       \
    } while (0)

#endif

#endif /* SENTINEL_DEBUG_PRINT_HPP */
