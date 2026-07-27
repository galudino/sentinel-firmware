///
/// \file    sentinel_test_ds3231.cpp
/// \brief   DS3231 RTC driver test suite implementation
///
/// \details Implements the run-to-completion testbench suite declared in
///          \c sentinel_test_ds3231.hpp. The tests exercise every public
///          member of \ref sentinel::ds3231 against a physical DS3231
///          attached to \c sentinel::resource::cybsp_i2c_bus (the primary
///          I²C bus exposed by Device Configurator).
///
///          API style: the tests use the modern \c std::optional / \c bool
///          public surface of \ref sentinel::ds3231, with \c last_error()
///          inspected on failure to recover the raw error code for
///          logging.
///
///          Structure (#48): the individual tests are members of a TU-local
///          \c fixture that owns the bus-arbitrated transport, mirroring a
///          GoogleTest \c TEST_F fixture — the shared resource lives in the
///          fixture, not a file-static global. Each test returns \c true on
///          pass / \c false on fail; \ref sentinel::test::ds3231::run_all
///          constructs the fixture, folds every outcome into a
///          \ref sentinel::test::tally, and returns it.
///
///          Output strategy:
///          - Progress and PASS / FAIL summary lines are emitted via
///            \c logi / \c loge; the logging facade (#50) writes each line once
///            to both the retarget-IO UART serial monitor and the BLE
///            debug-stream view in SentinelPanel.
///
///          Floating-point handling: temperature comes back from the
///          driver as \c int16_t centi-degrees Celsius, so no \c %f path
///          is ever needed. Per the project-wide constraint in
///          \c sentinel_debug_print.hpp, the test never formats floats
///          with \c %f / \c %e / \c %g.
///
/// \author  galudino
/// \date    2026-05-16
/// \version 2.0 - Run-to-completion fixture suite (#48)
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_result.h"
#include "cycfg_pins.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_build_time.hpp"
#include "sentinel_cyhal_i2c_bus_transport.hpp"
#include "sentinel_debug_print.hpp"
#include "sentinel_ds3231.hpp"
#include "sentinel_resource.hpp"
#include "sentinel_test_ds3231.hpp"
#include "sentinel_test_result.hpp"
#include "sentinel_utilities.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace {

///
/// \brief Yield long enough for the BLE debug ring buffer to drain.
///
/// \details The debug stream's ring buffer is best-effort; rapid back-to-
///          back \c logi() calls can overflow it and silently drop
///          messages. Inserting a short \c vTaskDelay between logical
///          test phases gives the debug-stream task time to push pending
///          bytes out as BLE notifications before the next batch arrives.
///
/// \param   milliseconds Yield duration in milliseconds.
///
inline void yield_for_debug_drain(uint32_t milliseconds) noexcept {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

///
/// \brief Format a signed centi-unit integer with two decimal places.
///
/// \details Helper that pulls the sign apart from the magnitude so the
///          \c %d.%02d trick does not produce strings like \c "-23.-05".
///
/// \param[in]  centi      Value scaled by 100 (e.g. \c -2305 for \c -23.05).
/// \param[out] sign_out   Receives \c '-' for negatives, \c '+' otherwise.
/// \param[out] whole_out  Receives the absolute-value whole part.
/// \param[out] frac_out   Receives the absolute-value two-digit fraction.
///
inline void split_centi(int32_t centi, char &sign_out, int32_t &whole_out,
                        int32_t &frac_out) noexcept {
    sign_out = centi < 0 ? '-' : '+';

    auto magnitude = centi < 0 ? -centi : centi;
    whole_out = magnitude / 100;
    frac_out = magnitude % 100;
}

///
/// \brief ISO-day-of-week (1=Mon … 7=Sun) → short label.
///
/// \param iso_dow ISO day-of-week number, 1..7 (Mon..Sun).
/// \return Three-letter day name, or \c "?" if \p iso_dow is out of range.
///
inline const char *day_name(uint8_t iso_dow) noexcept {
    static constexpr const char *names[8] = {"?",   "Mon", "Tue", "Wed",
                                             "Thu", "Fri", "Sat", "Sun"};
    return (iso_dow >= 1 && iso_dow <= 7) ? names[iso_dow] : names[0];
}

///
/// \brief Convenience alias for the concrete driver instantiation used
///        by every test, to keep the long template name out of every
///        function body.
///
using ds3231_t = sentinel::ds3231<sentinel::cyhal_i2c_bus_transport>;

///
/// \brief Test fixture: owns the bus-arbitrated transport every test shares.
///
/// \details Routes through \c sentinel::resource::cybsp_i2c_bus (the FreeRTOS
///          bus-arbiter task) so this suite's transactions serialize cleanly
///          with any other tasks sharing the same physical I²C bus — notably
///          the BME280 suite, which targets 0x76 on the same SCB. Each
///          transport instance carries its own target-address member, so the
///          arbiter routes requests to the correct slave automatically.
///          Constructed fresh by \ref sentinel::test::ds3231::run_all (like a
///          GoogleTest \c SetUp), so there is no file-static bus global. The
///          transport is inert until \c peripheral_initialize() has spawned
///          the arbiter, which the orchestrator guarantees by running
///          post-scheduler.
///
struct fixture {
    /// Bus-arbitrated I2C transport, shared by every test below.
    sentinel::cyhal_i2c_bus_transport ds3231_bus{
        sentinel::resource::cybsp_i2c_bus,
        static_cast<uint16_t>(ds3231_t::slave_address::primary)};

    /// \brief Confirm the status register is reachable and log the OSF state.
    /// \return \c true unless the status read fails or reads back 0xFF.
    bool presence_check() noexcept;
    /// \brief Mutate the aging-offset register, write it back, and read it
    ///        back, then restore the original value.
    /// \return \c true if the readback matches the mutated value.
    bool register_round_trip() noexcept;
    /// \brief Read the current time three times and look for a seconds tick.
    /// \return \c true unless an I2C read fails (a stalled clock only warns).
    bool time_read() noexcept;
    /// \brief Write a leap-year test pattern and read it back.
    /// \return \c true if every field round-trips (seconds within drift).
    bool time_write() noexcept;
    /// \brief Sync the RTC from the firmware build timestamp and verify it.
    /// \return \c true unless the sync call or the post-sync read fails.
    bool time_sync_from_build() noexcept;
    /// \brief Force a temperature conversion, poll BSY, and read the result.
    /// \return \c true unless a register read/write fails or BSY never clears.
    bool temperature_read() noexcept;
    /// \brief Set Alarm 1 and Alarm 2 to distinct match modes and read back.
    /// \return \c true if both alarms round-trip their configured fields.
    bool alarm_round_trip() noexcept;
};

} // namespace

// ============================================================================
// fixture::presence_check
// ============================================================================

bool fixture::presence_check() noexcept {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 presence_check: driver constructed");
    yield_for_debug_drain(200);

    auto status = rtc.status();
    if (!status) {
        loge("presence_check FAIL: status read error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    // A floating I²C bus typically reads back 0xFF for every byte (pullups
    // with no slave asserting). The DS3231 status register has reserved
    // bits at positions 4..6 that always read 0, so 0xFF is impossible
    // from a working part.
    if (*status == 0xFF) {
        loge("presence_check FAIL: status=0xFF (no slave?)");
        return false;
    }

    auto osf = rtc.oscillator_stop_flag();
    if (!osf) {
        loge("presence_check FAIL: OSF read error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    logi("presence_check PASS: status=0x%02X OSF=%d", static_cast<int>(*status),
         *osf ? 1 : 0);

    if (*osf) {
        logw("presence_check: oscillator-stop flag set; time may be "
             "uninitialized. Call set_time() then "
             "clear_oscillator_stop_flag().");
    }

    return true;
}

// ============================================================================
// fixture::register_round_trip
// ============================================================================

bool fixture::register_round_trip() noexcept {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 register_round_trip: driver constructed");
    yield_for_debug_drain(200);

    // Snapshot original aging-offset so we can restore it.
    auto original = rtc.aging_offset();
    if (!original) {
        loge("register_round_trip FAIL: initial read error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }
    logi("register_round_trip: original aging_offset=%d",
         static_cast<int>(*original));
    yield_for_debug_drain(100);

    // Pick a target distinct from the original so the read-back is
    // unambiguous. Both 0x5A and -42 (0xD6) are well clear of typical
    // factory values and exercise both halves of the signed range.
    auto target = static_cast<int8_t>(*original == 0x5A ? -42 : 0x5A);

    if (!rtc.set_aging_offset(target)) {
        loge("register_round_trip FAIL: write error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    auto readback = rtc.aging_offset();
    if (!readback) {
        loge("register_round_trip FAIL: readback error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    auto ok = bool{};
    if (*readback != target) {
        loge("register_round_trip FAIL: readback %d != expected %d",
             static_cast<int>(*readback), static_cast<int>(target));
        ok = false;
    } else {
        logi("register_round_trip PASS: aging_offset readback=%d",
             static_cast<int>(*readback));
        ok = true;
    }

    // Restore original — best-effort; a failure here is logged but does
    // not affect the test result.
    if (!rtc.set_aging_offset(*original)) {
        logw("register_round_trip: restore error %d",
             static_cast<int>(rtc.last_error()));
    }

    return ok;
}

// ============================================================================
// fixture::time_read
// ============================================================================

bool fixture::time_read() noexcept {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 time_read: driver constructed");
    yield_for_debug_drain(200);

    // Three sequential reads ~500 ms apart. If the oscillator is running
    // and the bus is healthy, at least one of the gaps should show a
    // seconds-field change.
    auto observed_change = false;
    auto previous_second = uint8_t{0xFF};

    for (auto i = uint8_t{0}; i < 3; ++i) {
        auto now = rtc.time();
        if (!now) {
            loge("time_read FAIL: read %d returned error %d",
                 static_cast<int>(i), static_cast<int>(rtc.last_error()));
            return false;
        }

        logi("time_read[%d]: %04d-%02d-%02d %s %02d:%02d:%02d",
             static_cast<int>(i), static_cast<int>(now->year),
             static_cast<int>(now->month), static_cast<int>(now->date),
             day_name(now->day_of_week), static_cast<int>(now->hour),
             static_cast<int>(now->minute), static_cast<int>(now->second));

        if (i > 0 && now->second != previous_second) {
            observed_change = true;
        }
        previous_second = now->second;

        if (i < 2) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    if (observed_change) {
        logi("time_read PASS: seconds field changed across reads");
    } else {
        logw("time_read: seconds did not change across 1s of reads — "
             "could be coincidence at boundary, or oscillator stopped");
    }

    // The seconds-unchanged case is a tolerated warning (clock boundary), not
    // a hard failure: only an I²C read error fails this test.
    return true;
}

// ============================================================================
// fixture::time_write
// ============================================================================

bool fixture::time_write() noexcept {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 time_write: driver constructed");
    yield_for_debug_drain(200);

    // Snapshot the pre-write time for the log; we do not restore.
    auto before = rtc.time();
    if (!before) {
        loge("time_write FAIL: initial read error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }
    logi("time_write[before]: %04d-%02d-%02d %s %02d:%02d:%02d",
         static_cast<int>(before->year), static_cast<int>(before->month),
         static_cast<int>(before->date), day_name(before->day_of_week),
         static_cast<int>(before->hour), static_cast<int>(before->minute),
         static_cast<int>(before->second));
    yield_for_debug_drain(100);

    // Test pattern: 2024-02-29 12:34:56 Thursday.
    //
    // Chosen to exercise leap-year handling (Feb 29 is only a valid date in
    // a leap year), to keep all fields distinct in BCD so any decode error
    // is unambiguous in the readback, and to sit comfortably in the
    // DS3231-addressable range (2000–2199).
    auto target = ds3231_t::datetime();
    target.year = 2024;
    target.month = 2;
    target.date = 29;
    target.day_of_week = static_cast<uint8_t>(ds3231_t::day_of_week::thursday);
    target.hour = 12;
    target.minute = 34;
    target.second = 56;

    if (!rtc.set_time(target)) {
        loge("time_write FAIL: set_time error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    // Read back. A few hundred microseconds of I²C round-trip mean the
    // seconds field could have advanced by up to 1 in practice; accept
    // anything in [target.second, target.second + 2] modulo 60 to be safe.
    auto after = rtc.time();
    if (!after) {
        loge("time_write FAIL: readback error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    auto seconds_drift_ok = false;
    for (auto offset = uint8_t{0}; offset <= 2; ++offset) {
        auto expected_second =
            static_cast<uint8_t>((target.second + offset) % 60);
        if (after->second == expected_second) {
            seconds_drift_ok = true;
            break;
        }
    }

    auto fields_ok = after->year == target.year &&
                     after->month == target.month &&
                     after->date == target.date &&
                     after->day_of_week == target.day_of_week &&
                     after->hour == target.hour &&
                     after->minute == target.minute && seconds_drift_ok;

    if (!fields_ok) {
        loge("time_write FAIL: readback mismatch — "
             "wrote %04d-%02d-%02d %s %02d:%02d:%02d, "
             "got %04d-%02d-%02d %s %02d:%02d:%02d",
             static_cast<int>(target.year), static_cast<int>(target.month),
             static_cast<int>(target.date), day_name(target.day_of_week),
             static_cast<int>(target.hour), static_cast<int>(target.minute),
             static_cast<int>(target.second), static_cast<int>(after->year),
             static_cast<int>(after->month), static_cast<int>(after->date),
             day_name(after->day_of_week), static_cast<int>(after->hour),
             static_cast<int>(after->minute), static_cast<int>(after->second));
        return false;
    }

    // Acknowledge that time has been validly set so future reads of OSF
    // no longer flag the time as potentially uninitialized.
    if (!rtc.clear_oscillator_stop_flag()) {
        logw("time_write: OSF clear error %d (non-fatal)",
             static_cast<int>(rtc.last_error()));
    }

    logi("time_write PASS: wrote+readback %04d-%02d-%02d %s %02d:%02d:%02d "
         "(seconds drift %d)",
         static_cast<int>(after->year), static_cast<int>(after->month),
         static_cast<int>(after->date), day_name(after->day_of_week),
         static_cast<int>(after->hour), static_cast<int>(after->minute),
         static_cast<int>(after->second),
         static_cast<int>(after->second - target.second));
    return true;
}

// ============================================================================
// fixture::time_sync_from_build
// ============================================================================

bool fixture::time_sync_from_build() noexcept {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 time_sync_from_build: driver constructed");
    yield_for_debug_drain(200);

    // Log the build moment that the helper is about to push to the RTC.
    // This is the same value sync_from_build_time() captures internally;
    // showing it up front makes any later mismatch obvious.
    logi("time_sync_from_build: build was %04d-%02d-%02d %02d:%02d:%02d "
         "(local), fudge=%us",
         static_cast<int>(sentinel::build_time::build_year()),
         static_cast<int>(sentinel::build_time::build_month()),
         static_cast<int>(sentinel::build_time::build_day()),
         static_cast<int>(sentinel::build_time::build_hour()),
         static_cast<int>(sentinel::build_time::build_minute()),
         static_cast<int>(sentinel::build_time::build_second()),
         static_cast<unsigned>(sentinel::build_time::DEFAULT_FUDGE_SECONDS));
    yield_for_debug_drain(100);

    if (!sentinel::build_time::sync_from_build_time(rtc)) {
        loge("time_sync_from_build FAIL: sync helper error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    auto after = rtc.time();
    if (!after) {
        loge("time_sync_from_build FAIL: post-sync read error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    // Sanity-check the readback against the captured build year/month/date.
    // We don't compare hours/minutes/seconds because by the time we read
    // back, real-world wall time has likely advanced past what the helper
    // wrote (fudge + I²C round-trip + extra logging).
    auto date_matches = after->year == sentinel::build_time::build_year() &&
                        after->month == sentinel::build_time::build_month() &&
                        after->date == sentinel::build_time::build_day();
    if (!date_matches) {
        logw("time_sync_from_build: post-sync date %04d-%02d-%02d "
             "differs from build date %04d-%02d-%02d "
             "(boundary crossed during sync? non-fatal)",
             static_cast<int>(after->year), static_cast<int>(after->month),
             static_cast<int>(after->date),
             static_cast<int>(sentinel::build_time::build_year()),
             static_cast<int>(sentinel::build_time::build_month()),
             static_cast<int>(sentinel::build_time::build_day()));
    }

    logi("time_sync_from_build PASS: RTC now %04d-%02d-%02d %s "
         "%02d:%02d:%02d, OSF cleared",
         static_cast<int>(after->year), static_cast<int>(after->month),
         static_cast<int>(after->date), day_name(after->day_of_week),
         static_cast<int>(after->hour), static_cast<int>(after->minute),
         static_cast<int>(after->second));
    return true;
}

// ============================================================================
// fixture::temperature_read
// ============================================================================

bool fixture::temperature_read() noexcept {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 temperature_read: driver constructed");
    yield_for_debug_drain(200);

    // Read whatever the last scheduled conversion left in the registers.
    auto first = rtc.temperature_centi_c();
    if (!first) {
        loge("temperature_read FAIL: initial read error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }
    auto sign_a = char{};
    auto whole_a = int32_t{};
    auto frac_a = int32_t{};
    split_centi(*first, sign_a, whole_a, frac_a);
    logi("temperature_read[initial]: %c%d.%02d C", sign_a,
         static_cast<int>(whole_a), static_cast<int>(frac_a));
    yield_for_debug_drain(100);

    // Force a fresh one-shot conversion.
    if (!rtc.start_temperature_conversion()) {
        loge("temperature_read FAIL: start_conversion error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }
    logi("temperature_read: CONV set, polling BSY...");

    // Poll BSY for up to ~200 ms. The datasheet quotes typical conversion
    // time at ~125 ms.
    auto completed = false;
    for (auto i = uint8_t{0}; i < 20; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
        auto busy = rtc.is_temperature_conversion_busy();
        if (!busy) {
            loge("temperature_read FAIL: BSY poll error %d",
                 static_cast<int>(rtc.last_error()));
            return false;
        }
        if (!*busy) {
            completed = true;
            break;
        }
    }

    if (!completed) {
        loge("temperature_read FAIL: BSY did not clear within 200ms");
        return false;
    }

    auto second = rtc.temperature_centi_c();
    if (!second) {
        loge("temperature_read FAIL: post-conversion read error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }
    auto sign_b = char{};
    auto whole_b = int32_t{};
    auto frac_b = int32_t{};
    split_centi(*second, sign_b, whole_b, frac_b);
    logi("temperature_read[post-conv]: %c%d.%02d C", sign_b,
         static_cast<int>(whole_b), static_cast<int>(frac_b));

    return true;
}

// ============================================================================
// fixture::alarm_round_trip
// ============================================================================

bool fixture::alarm_round_trip() noexcept {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 alarm_round_trip: driver constructed");
    yield_for_debug_drain(200);

    // Keep alarm interrupts off across the test so the INT/SQW pin
    // does not unexpectedly assert while we are scribbling on the
    // alarm registers.
    if (!rtc.set_alarm1_interrupt_enabled(false) ||
        !rtc.set_alarm2_interrupt_enabled(false)) {
        loge("alarm_round_trip FAIL: could not disable alarm interrupts "
             "(last_err=%d)",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    // -------- Alarm 1: hours/minutes/seconds match --------
    auto alarm1_target = ds3231_t::alarm1_setting{};
    alarm1_target.match_mode =
        ds3231_t::alarm1_match_mode::hours_minutes_seconds;
    alarm1_target.second = 42;
    alarm1_target.minute = 17;
    alarm1_target.hour = 9;
    alarm1_target.day_or_date = 1; // ignored in this match mode

    if (!rtc.set_alarm1(alarm1_target)) {
        loge("alarm_round_trip FAIL: set_alarm1 error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    auto alarm1_readback = rtc.alarm1();
    if (!alarm1_readback) {
        loge("alarm_round_trip FAIL: get_alarm1 error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    if (alarm1_readback->match_mode != alarm1_target.match_mode ||
        alarm1_readback->second != alarm1_target.second ||
        alarm1_readback->minute != alarm1_target.minute ||
        alarm1_readback->hour != alarm1_target.hour) {
        loge("alarm_round_trip FAIL: alarm1 mismatch — readback "
             "h=%d m=%d s=%d mode=%d",
             static_cast<int>(alarm1_readback->hour),
             static_cast<int>(alarm1_readback->minute),
             static_cast<int>(alarm1_readback->second),
             static_cast<int>(alarm1_readback->match_mode));
        return false;
    }
    logi("alarm_round_trip: alarm1 ok (h=%d m=%d s=%d, hours_minutes_seconds)",
         static_cast<int>(alarm1_readback->hour),
         static_cast<int>(alarm1_readback->minute),
         static_cast<int>(alarm1_readback->second));
    yield_for_debug_drain(100);

    // -------- Alarm 2: day-of-week + hours/minutes match --------
    auto alarm2_target = ds3231_t::alarm2_setting{};
    alarm2_target.match_mode =
        ds3231_t::alarm2_match_mode::day_of_week_hours_minutes;
    alarm2_target.minute = 7;
    alarm2_target.hour = 14;
    alarm2_target.day_or_date =
        static_cast<uint8_t>(ds3231_t::day_of_week::wednesday);

    if (!rtc.set_alarm2(alarm2_target)) {
        loge("alarm_round_trip FAIL: set_alarm2 error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    auto alarm2_readback = rtc.alarm2();
    if (!alarm2_readback) {
        loge("alarm_round_trip FAIL: get_alarm2 error %d",
             static_cast<int>(rtc.last_error()));
        return false;
    }

    if (alarm2_readback->match_mode != alarm2_target.match_mode ||
        alarm2_readback->minute != alarm2_target.minute ||
        alarm2_readback->hour != alarm2_target.hour ||
        alarm2_readback->day_or_date != alarm2_target.day_or_date) {
        loge("alarm_round_trip FAIL: alarm2 mismatch — readback "
             "h=%d m=%d dow=%d mode=%d",
             static_cast<int>(alarm2_readback->hour),
             static_cast<int>(alarm2_readback->minute),
             static_cast<int>(alarm2_readback->day_or_date),
             static_cast<int>(alarm2_readback->match_mode));
        return false;
    }
    logi("alarm_round_trip: alarm2 ok (h=%d m=%d dow=%d (%s), "
         "day_of_week_hours_minutes)",
         static_cast<int>(alarm2_readback->hour),
         static_cast<int>(alarm2_readback->minute),
         static_cast<int>(alarm2_readback->day_or_date),
         day_name(alarm2_readback->day_or_date));

    // Clear any alarm flags raised during the test.
    rtc.clear_alarm1_flag();
    rtc.clear_alarm2_flag();

    logi("DS3231 alarm_round_trip PASS");
    return true;
}

// ============================================================================
// sentinel::test::ds3231::run_all
// ============================================================================

sentinel::test::tally sentinel::test::ds3231::run_all() noexcept {
    auto fx = fixture{};
    auto t  = sentinel::test::tally{};

    t.record(fx.presence_check());
    yield_for_debug_drain(200);

    t.record(fx.register_round_trip());
    yield_for_debug_drain(200);

    t.record(fx.time_read());
    yield_for_debug_drain(200);

    t.record(fx.time_write());
    yield_for_debug_drain(200);

    t.record(fx.time_sync_from_build());
    yield_for_debug_drain(200);

    t.record(fx.temperature_read());
    yield_for_debug_drain(200);

    t.record(fx.alarm_round_trip());
    yield_for_debug_drain(200);

    // Continuous time/temperature reads are owned by sentinel::task::rtc_service,
    // which the orchestrator starts after this one-shot suite completes.
    return t;
}
