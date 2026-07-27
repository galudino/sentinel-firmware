///
/// \file    sentinel_build_time.hpp
/// \brief   Compile-time build-timestamp parsers and RTC sync helper
///
/// \details This header captures the build moment of the translation unit
///          that includes it, by parsing the standard preprocessor macros
///          \c __DATE__ ("Mmm DD YYYY") and \c __TIME__ ("HH:MM:SS") into
///          discrete numeric fields at compile time. Every field is a
///          \c constexpr value derived from a string literal, so there is
///          zero runtime cost beyond the eventual register writes when the
///          captured time is pushed to an RTC.
///
///          Intended use:
///          - As the bring-up time-sync path for a freshly-flashed board
///            that has no client app yet.
///          - \ref sentinel::build_time::sync_from_build_time wraps the
///            capture-and-push sequence for any RTC driver whose interface
///            matches the \ref sentinel::ds3231 shape
///            (nested \c datetime with \c to_unix_time / \c from_unix_time
///            static helpers, \c set_time(const datetime&) returning
///            \c bool, and \c clear_oscillator_stop_flag()).
///
///          Caveats:
///          - \c __DATE__ and \c __TIME__ reflect the build host's
///            \b local time, not UTC. The RTC will therefore hold local
///            time unless the caller passes a non-zero
///            \c tz_offset_seconds to compensate.
///          - The expansion happens once per translation unit; in a
///            multi-TU build different TUs see different build times
///            (typically within a few seconds of each other). Practically
///            this is a non-issue because
///            \ref sentinel::build_time::sync_from_build_time is usually
///            called from one TU.
///          - The captured time runs as far behind real wall-clock time
///            as it takes to link, sign, flash, and boot the firmware.
///            \c fudge_seconds in
///            \ref sentinel::build_time::sync_from_build_time exists to
///            compensate; the default (10 s) is a reasonable estimate
///            for KitProg3-driven flash + cold boot on a CYBLE-416045-EVAL.
///
/// \author  galudino
/// \date    2026-05-16
/// \version 1.0 - Initial implementation
///

#ifndef SENTINEL_BUILD_TIME_HPP
#define SENTINEL_BUILD_TIME_HPP

#include <cstdint>

namespace sentinel::build_time {

// ============================================================================
// __DATE__ field parsers
// ============================================================================

///
/// \brief Compile-time month-of-year captured from \c __DATE__.
///
/// \details \c __DATE__ expands to a three-letter English month abbreviation
///          followed by the day and year. This function distinguishes the
///          12 months purely by character comparison.
///
/// \return Month as 1–12, or 0 if the macro string did not start with one
///         of the twelve recognised characters.
///
constexpr uint8_t build_month() noexcept {
    auto const d = __DATE__;
    switch (d[0]) {
        case 'J':
            // Jan, Jun, or Jul
            if (d[1] == 'a') return 1;
            return d[2] == 'n' ? uint8_t{6} : uint8_t{7};
        case 'F':
            return 2; // Feb
        case 'M':
            // Mar or May
            return d[2] == 'r' ? uint8_t{3} : uint8_t{5};
        case 'A':
            // Apr or Aug
            return d[1] == 'p' ? uint8_t{4} : uint8_t{8};
        case 'S':
            return 9; // Sep
        case 'O':
            return 10; // Oct
        case 'N':
            return 11; // Nov
        case 'D':
            return 12; // Dec
        default:
            return 0;
    }
}

///
/// \brief Compile-time day-of-month captured from \c __DATE__.
///
/// \details The day field is two characters at offsets 4–5 of \c __DATE__,
///          with offset 4 being either a tens digit or a space (for days
///          1–9).
///
/// \return Day as 1–31.
///
constexpr uint8_t build_day() noexcept {
    auto const d    = __DATE__;
    auto const tens = (d[4] == ' ') ? uint8_t{0}
                                    : static_cast<uint8_t>(d[4] - '0');
    auto const ones = static_cast<uint8_t>(d[5] - '0');
    return static_cast<uint8_t>(tens * 10 + ones);
}

///
/// \brief Compile-time four-digit year captured from \c __DATE__.
///
/// \return Year (e.g. 2026).
///
constexpr uint16_t build_year() noexcept {
    auto const d = __DATE__;
    return static_cast<uint16_t>((d[7]  - '0') * 1000
                               + (d[8]  - '0') * 100
                               + (d[9]  - '0') * 10
                               + (d[10] - '0'));
}

// ============================================================================
// __TIME__ field parsers
// ============================================================================

///
/// \brief Compile-time hour-of-day captured from \c __TIME__ (0–23).
///
/// \return Hour as 0–23.
///
constexpr uint8_t build_hour() noexcept {
    auto const t = __TIME__;
    return static_cast<uint8_t>((t[0] - '0') * 10 + (t[1] - '0'));
}

///
/// \brief Compile-time minute-of-hour captured from \c __TIME__ (0–59).
///
/// \return Minute as 0–59.
///
constexpr uint8_t build_minute() noexcept {
    auto const t = __TIME__;
    return static_cast<uint8_t>((t[3] - '0') * 10 + (t[4] - '0'));
}

///
/// \brief Compile-time second-of-minute captured from \c __TIME__ (0–59).
///
/// \return Second as 0–59.
///
constexpr uint8_t build_second() noexcept {
    auto const t = __TIME__;
    return static_cast<uint8_t>((t[6] - '0') * 10 + (t[7] - '0'));
}

// ============================================================================
// Sync helper
// ============================================================================

///
/// \brief Default flash + boot fudge for KitProg3-driven CYBLE-416045-EVAL.
///
/// \details Reasonable estimate of the latency between the build host
///          recording \c __TIME__ and the application reaching the first
///          \c sync_from_build_time call: enough time for linking, hex
///          signing, KitProg3 to flash, and the part to reset and reach
///          the test task.
///
inline constexpr uint32_t DEFAULT_FUDGE_SECONDS = 10;

///
/// \brief Set an RTC to the build moment (plus a small fudge).
///
/// \details Builds a \c RtcDriver::datetime from
///          \ref sentinel::build_time::build_year through
///          \ref sentinel::build_time::build_second, runs it through the
///          driver's
///          \c to_unix_time/from_unix_time round-trip to recover the
///          correct day-of-week (and to apply \p fudge_seconds and
///          \p tz_offset_seconds in seconds-space rather than worrying
///          about calendar arithmetic), then calls
///          \c set_time followed by \c clear_oscillator_stop_flag.
///
/// \tparam RtcDriver A driver type matching the \ref sentinel::ds3231
///                   interface shape (see file header).
/// \param  rtc                Driver instance bound to a working transport.
/// \param  fudge_seconds      Seconds to add to the captured build time to
///                            account for flash + boot latency. Defaults
///                            to \ref
///                            sentinel::build_time::DEFAULT_FUDGE_SECONDS.
/// \param  tz_offset_seconds  Seconds to subtract from the captured local
///                            build time to convert it to UTC. Pass \c 0
///                            (the default) to treat the build time as
///                            UTC; pass e.g. \c -5*3600 if the build host
///                            is in EST and you want the RTC to hold UTC.
/// \return \c true on success; \c false if any of the conversions or RTC
///         writes failed (consult \c rtc.last_error() for detail).
///
/// \note On failure mid-sequence, the RTC may be left in a partially-
///       updated state. For the smoke-test bring-up use case this is
///       acceptable; production callers should follow up with a read-back
///       check.
///
template <typename RtcDriver>
bool sync_from_build_time(RtcDriver &rtc,
                          uint32_t fudge_seconds = DEFAULT_FUDGE_SECONDS,
                          int32_t  tz_offset_seconds = 0) noexcept {
    using datetime_t = typename RtcDriver::datetime;

    auto dt        = datetime_t{};
    dt.year        = build_year();
    dt.month       = build_month();
    dt.date        = build_day();
    dt.hour        = build_hour();
    dt.minute      = build_minute();
    dt.second      = build_second();
    // day_of_week is corrected by the unix-time round-trip below; default
    // to 1 (Monday) here purely so is_valid() accepts the placeholder.
    dt.day_of_week = 1;

    auto unix_seconds = datetime_t::to_unix_time(dt);
    if (!unix_seconds) return false;

    // tz_offset_seconds is in the conventional sense: local = UTC + offset,
    // so UTC = local - offset.
    auto adjusted = static_cast<int64_t>(*unix_seconds)
                  + static_cast<int64_t>(fudge_seconds)
                  - static_cast<int64_t>(tz_offset_seconds);
    if (adjusted < 0) return false;

    auto corrected = datetime_t::from_unix_time(
        static_cast<uint32_t>(adjusted));
    if (!corrected) return false;

    if (!rtc.set_time(*corrected)) return false;
    return rtc.clear_oscillator_stop_flag();
}

} // namespace sentinel::build_time

#endif /* SENTINEL_BUILD_TIME_HPP */
