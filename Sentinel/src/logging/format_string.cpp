///
/// \file       format_string.cpp
/// \brief      String Formatting Utilities - Implementation
///
/// Implements lightweight string formatting utilities for log messages
/// and timestamp formatting in embedded environments.
///
/// NOTE: Avoids <time.h> functions (gmtime_r, strftime) which are heavy
/// in embedded newlib and can cause startup issues.
///
/// \author     galudino
/// \date       2026-01-31
///

#include "format_string.hpp"

#include <cstdio>

namespace logging {

struct datetime {
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
};

//==============================================================================
// Module-private constants
//==============================================================================

/// Days in each month (non-leap year)
static constexpr int days_in_month[] = {31, 28, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31};

//==============================================================================
// Module-private function prototypes
//==============================================================================

static bool is_leap_year(int year);
static datetime unix_seconds_to_datetime(int64_t unix_seconds);

} // namespace logging

//==============================================================================
// Public API Implementation
//==============================================================================

int logging::format_unix_timestamp_ms(int64_t unix_time_ms, char *out_buf,
                                      size_t out_buf_size, bool local_time) {
    static_cast<void>(local_time); // UTC only for simplicity

    int64_t seconds = unix_time_ms / 1000;
    auto milliseconds = static_cast<int>(unix_time_ms % 1000);

    auto dt = unix_seconds_to_datetime(seconds);

    // Format: "YYYY-MM-DD HH:MM:SS.mmmZ"
    auto length = std::snprintf(
        out_buf, out_buf_size, "%04d-%02d-%02d %02d:%02d:%02d.%03dZ", dt.year,
        dt.month, dt.day, dt.hour, dt.min, dt.sec, milliseconds);

    return length;
}

int logging::build_string(char *out, size_t size, uint64_t unix_time_ms,
                          const char *file, int line, const char *function,
                          const char *level, const char *fmt, va_list args) {
    // Get basename of file (strip path)
    const char *basename = file;
    for (auto *p = file; *p; p++) {
        if (*p == '/' || *p == '\\') {
            basename = p + 1;
        }
    }

    // Format timestamp directly into the output buffer first to avoid
    // a separate 28-byte local array. We'll overwrite it with the full
    // prefix using snprintf below.
    //
    // NOTE: We use %.*s precision specifiers to truncate file/function
    // names inline — this avoids local char arrays and saves ~32 bytes
    // of stack, which matters on tasks with 200-300 word stacks.
    char timestamp[28];
    format_unix_timestamp_ms(static_cast<int64_t>(unix_time_ms), timestamp,
                             sizeof(timestamp), false);

    // Build the log prefix: "<timestamp> [file:line] function <level> "
    // %.*s truncates file to 16 chars and function to 14 chars inline
    auto written =
        std::snprintf(out, size, "<%s> [%.*s:%d] %.*s <%s> ", timestamp, 16,
                      basename, line, 14, function, level);

    if (written < 0 || static_cast<size_t>(written) >= size) {
        return written;
    }

    // Calculate how much space remains for the message
    // Reserve 2 chars for \n and \0
    size_t remaining = size - static_cast<size_t>(written);
    size_t max_msg = (remaining > 2) ? (remaining - 2) : 0;

    // Format the message directly into the remaining buffer space
    // This uses ALL remaining space - no artificial 48-char limit!
    if (max_msg > 0) {
        int msg_len = vsnprintf(out + written, max_msg + 1, fmt, args);

        if (msg_len > 0) {
            size_t actual = (static_cast<size_t>(msg_len) < max_msg)
                                ? static_cast<size_t>(msg_len)
                                : max_msg;
            written += static_cast<int>(actual);
        }
    }

    // Append newline if there's room
    if (static_cast<size_t>(written) + 1 < size) {
        out[written++] = '\n';
    }
    out[written] = '\0';

    return written;
}

//==============================================================================
// Module-private implementation
//==============================================================================

///
/// \brief      Check if a year is a leap year.
///
static bool logging::is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

///
/// \brief      Convert Unix timestamp (seconds since 1970) to date/time
/// components.
///
/// \return     datetime struct with year, month, day, hour, min, sec
///
/// Lightweight alternative to gmtime_r() that doesn't require heavy libc
/// functions.
///
static logging::datetime
logging::unix_seconds_to_datetime(int64_t unix_seconds) {
    auto dt = datetime{};

    // Handle time of day first (simpler math)
    int64_t days_since_epoch = unix_seconds / 86400;
    auto seconds_in_day = static_cast<int>(unix_seconds % 86400);

    if (seconds_in_day < 0) {
        seconds_in_day += 86400;
        days_since_epoch--;
    }

    dt.hour = seconds_in_day / 3600;
    dt.min = (seconds_in_day % 3600) / 60;
    dt.sec = seconds_in_day % 60;

    // Calculate year, month, day from days since epoch
    // Epoch is January 1, 1970
    auto y = 1970;

    while (days_since_epoch >= (is_leap_year(y) ? 366 : 365)) {
        days_since_epoch -= (is_leap_year(y) ? 366 : 365);
        ++y;
    }

    while (days_since_epoch < 0) {
        --y;
        days_since_epoch += (is_leap_year(y) ? 366 : 365);
    }

    dt.year = y;

    // Find month and day
    auto m = 0;
    auto days_this_month = 0;

    while (m < 12) {
        days_this_month = days_in_month[m];
        if (m == 1 && is_leap_year(y)) {
            days_this_month = 29; // February in leap year
        }

        if (days_since_epoch < days_this_month) {
            break;
        }

        days_since_epoch -= days_this_month;
        ++m;
    }

    dt.month = m + 1;                                // 1-based month
    dt.day = static_cast<int>(days_since_epoch) + 1; // 1-based day

    return dt;
}
