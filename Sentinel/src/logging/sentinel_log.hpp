///
/// \file       sentinel_log.hpp
/// \brief      Unified logging facade - Header
///
/// \details    One logging call, formatted once, fanned out to every configured
///             sink (serial + BLE debug stream today; RTT/stdout on other
///             platforms). Replaces the former split between \c cy_log_msg (an
///             Infineon-specific serial path) and the BLE-only \c
///             logi/logw/etc. A caller writes a message **once** and it reaches
///             both sinks intact.
///
///             Call sites use the \c logd / \c logi / \c logw / \c loge macros,
///             which capture \c __FILE__ / \c __LINE__ / \c __func__ and
///             forward to \ref sentinel::logging::log. That function carries a
///             \c printf format attribute, so the compiler type-checks every
///             call under \c -Werror.
///
/// \warning    NEVER use \c %f / \c %e / \c %g (float format specifiers) in
///             \c logd/logi/logw/loge calls. \c vsnprintf float-to-string
///             conversion uses 500-1500+ bytes of stack on ARM Cortex-M newlib,
///             which overflows tasks with 200-300 word stacks and corrupts
///             memory. Use integer casts instead: \c static_cast<int>(f) or
///             \c static_cast<int>(f * N).
///
/// \author     galudino
/// \date       2026-07-08
///

#ifndef SENTINEL_LOG_HPP
#define SENTINEL_LOG_HPP

#include "sentinel_log_sink.hpp"

#include <cstdarg>

/// Master logging enable/disable. Set to 0 to compile out all app logging
/// (serial + BLE). (Retained name from the former BLE-only debug stream.)
#define BLE_DEBUG_ENABLE 1

namespace sentinel::logging {

///
/// \brief      Initialize the logging facade (sink mutex + state).
///
/// \details    Call once during system bring-up, before the first log call,
///             in place of the former \c cy_log_init. Idempotent.
///
void init() noexcept;

///
/// \brief      Format a log line once and fan it out to every configured sink.
///
/// \details    Builds the line \c "<timestamp> [file:line] function <level>
///             message" a single time (wall clock from the DS3231 via the RTC
///             service cache), then hands the identical bytes to the serial and
///             BLE sinks. Prefer the \c logd/logi/logw/loge macros over calling
///             this directly.
///
/// \param      l           Severity level.
/// \param      file        Source filename (\c __FILE__).
/// \param      line        Source line number (\c __LINE__).
/// \param      function    Enclosing function name (\c __func__).
/// \param      fmt         printf-style format string.
/// \param      ...         Format arguments.
///
void log(level l, const char *file, int line, const char *function,
         const char *fmt, ...) __attribute__((format(printf, 5, 6)));

///
/// \brief      \c va_list form of \ref sentinel::logging::log.
///
/// \param      l           Severity level.
/// \param      file        Source filename.
/// \param      line        Source line number.
/// \param      function    Enclosing function name.
/// \param      fmt         printf-style format string.
/// \param      args        Format arguments as a \c va_list.
///
void vlog(level l, const char *file, int line, const char *function,
          const char *fmt, va_list args) __attribute__((format(printf, 5, 0)));

} // namespace sentinel::logging

// The format string rides in __VA_ARGS__ (not a named parameter) so a call with
// no format arguments — logi("text") — still expands with >= 1 macro argument.
// This avoids the GNU `, ##__VA_ARGS__` extension, which is a hard error under
// -pedantic-errors, without forcing callers to pass a dummy trailing "".

#if BLE_DEBUG_ENABLE

/// Log a debug-level message to every configured sink.
#define logd(...)                                                              \
    sentinel::logging::log(sentinel::logging::level::debug, __FILE__,          \
                           __LINE__, __func__, __VA_ARGS__)

/// Log an info-level message to every configured sink.
#define logi(...)                                                              \
    sentinel::logging::log(sentinel::logging::level::info, __FILE__, __LINE__, \
                           __func__, __VA_ARGS__)

/// Log a warning-level message to every configured sink.
#define logw(...)                                                              \
    sentinel::logging::log(sentinel::logging::level::warn, __FILE__, __LINE__, \
                           __func__, __VA_ARGS__)

/// Log an error-level message to every configured sink.
#define loge(...)                                                              \
    sentinel::logging::log(sentinel::logging::level::error, __FILE__,          \
                           __LINE__, __func__, __VA_ARGS__)

#else

/// Compiled out: \c BLE_DEBUG_ENABLE is 0, so debug-level logging is a no-op.
#define logd(...)                                                              \
    do {                                                                       \
    } while (0)
/// Compiled out: \c BLE_DEBUG_ENABLE is 0, so info-level logging is a no-op.
#define logi(...)                                                              \
    do {                                                                       \
    } while (0)
/// Compiled out: \c BLE_DEBUG_ENABLE is 0, so warning-level logging is a no-op.
#define logw(...)                                                              \
    do {                                                                       \
    } while (0)
/// Compiled out: \c BLE_DEBUG_ENABLE is 0, so error-level logging is a no-op.
#define loge(...)                                                              \
    do {                                                                       \
    } while (0)

#endif /* BLE_DEBUG_ENABLE */

#endif /* SENTINEL_LOG_HPP */
