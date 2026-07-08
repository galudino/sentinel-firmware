///
/// \file       sentinel_format_string.hpp
/// \brief      String Formatting Utilities - Header
///
/// Provides lightweight, reusable string formatting utilities for log messages
/// and timestamp formatting. Designed for use in embedded environments where
/// standard library printf may be unavailable or too heavy.
///
/// Features:
/// - Unix timestamp formatting with millisecond precision
/// - Structured log string building with metadata (file, line, function, level)
/// - UTC and local time support
/// - Buffer-safe operations with size checking
///
/// \author     galudino
/// \date       2026-05-15
///

#ifndef SENTINEL_FORMAT_STRING_HPP
#define SENTINEL_FORMAT_STRING_HPP

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace sentinel::logging {

//==============================================================================
// Public API
//==============================================================================

///
/// \brief      Format a Unix timestamp (milliseconds) into a human-readable
///             ISO 8601-style string.
///
/// Output format: "YYYY-MM-DD HH:MM:SS.mmmZ" (UTC) or
///                "YYYY-MM-DD HH:MM:SS.mmm" (local time)
///
/// \param      unix_time_ms    Unix timestamp in milliseconds
/// \param      out_buf         Output buffer for the formatted string
/// \param      out_buf_size    Size of the output buffer (minimum 28 bytes)
/// \param      local_time      true for local time, false for UTC (appends 'Z')
///
/// \return     Number of characters written (excluding null terminator),
///             or negative value on error
///
int format_unix_timestamp_ms(int64_t unix_time_ms, char *out_buf,
                             size_t out_buf_size, bool local_time);

//
/// \brief      Build a complete log line with sequence, timestamp, and
///             metadata.
///
/// Constructs a formatted log entry in the form:
/// "<seq> <timestamp> [file:line] function <level> message"
///
/// No trailing terminator is appended — the caller's sink adds its own
/// ('\n' for serial, '\0' for the BLE datagram). The buffer is null-terminated.
///
/// Example output:
/// "0042 <2026-01-31 18:00:24.027Z> [main.cpp:42] MyFunc <info> Hello, world!"
///
/// \param      out             Output buffer for the complete log string
/// \param      size            Size of the output buffer
/// \param      seq             Monotonic line index (also the fragment msg_id)
/// \param      unix_time_ms    Unix timestamp in milliseconds
/// \param      file            Source filename (typically __FILE__)
/// \param      line            Source line number (typically __LINE__)
/// \param      function        Function name (typically __func__)
/// \param      level           Log level string (e.g., "debug", "info",
/// "error")
/// \param      fmt             printf-style format string for the message
/// \param      args            Variable argument list for the format string
///
/// \return     Number of characters written (excluding null terminator),
///             or negative value on error
///
int build_string(char *out, size_t size, uint16_t seq, uint64_t unix_time_ms,
                 const char *file, int line, const char *function,
                 const char *level, const char *fmt, va_list args);

} // namespace sentinel::logging

#endif /* SENTINEL_FORMAT_STRING_HPP */
