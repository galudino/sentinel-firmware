///
/// \file    sentinel_test_ds3231.cpp
/// \brief   DS3231 RTC driver test implementations
///
/// \details Implements the testbench smoke tests declared in
///          \c sentinel_test_ds3231.hpp. The tests exercise every public
///          member of \ref sentinel::ds3231 against a physical DS3231
///          attached to \c sentinel::resource::cybsp_i2c (the primary
///          I²C bus exposed by Device Configurator).
///
///          API style: the tests use the modern \c std::optional / \c bool
///          public surface of \ref sentinel::ds3231, with \c last_error()
///          inspected on failure to recover the raw error code for
///          logging.
///
///          Output strategy:
///          - Step-by-step progress is emitted via \c logi / \c loge so
///            it appears in the BLE debug-stream view in SentinelPanel.
///          - PASS / FAIL summary lines are *also* emitted via
///            \c cy_log_msg so they are visible on the retarget-IO UART
///            even if the BLE debug-stream ring buffer overflows or no
///            BLE central is connected.
///
///          Floating-point handling: temperature comes back from the
///          driver as \c int16_t centi-degrees Celsius, so no \c %f path
///          is ever needed. Per the project-wide constraint in
///          \c sentinel_debug_print.hpp, the test never formats floats
///          with \c %f / \c %e / \c %g.
///
/// \author  galudino
/// \date    2026-05-16
/// \version 1.0 - DS3231 test implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_log.h"
#include "cy_result.h"
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
#include "sentinel_utilities.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace {

///
/// \brief Bus-arbitrated transport instance used by every DS3231 test.
///
/// \details Routes through \c sentinel::resource::cybsp_i2c_bus (the
///          FreeRTOS bus-arbiter task) so this test's transactions
///          serialize cleanly with any other tasks sharing the same
///          physical I²C bus — notably the BME280 test, which targets
///          0x76 on the same SCB. Each transport instance carries its
///          own target-address member, so the arbiter routes requests
///          to the correct slave automatically. Storage lives in BSS
///          until \c peripheral_initialize() spawns the arbiter; the
///          transport itself is inert until then.
///
sentinel::cyhal_i2c_bus_transport ds3231_bus(
    sentinel::resource::cybsp_i2c_bus,
    static_cast<uint16_t>(
        sentinel::ds3231<
            sentinel::cyhal_i2c_bus_transport>::slave_address::primary));

///
/// \brief Yield long enough for the BLE debug ring buffer to drain.
///
/// \details The debug stream's ring buffer is 256 bytes; rapid back-to-
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

} // namespace

// ============================================================================
// sentinel::test::ds3231::all
// ============================================================================

void sentinel::test::ds3231::all() {
    presence_check();
    yield_for_debug_drain(200);

    register_round_trip();
    yield_for_debug_drain(200);

    time_read();
    yield_for_debug_drain(200);

    time_write();
    yield_for_debug_drain(200);

    time_sync_from_build();
    yield_for_debug_drain(200);

    temperature_read();
    yield_for_debug_drain(200);

    alarm_round_trip();
    yield_for_debug_drain(200);

    // Never returns — runs forever at 1 Hz.
    continuous_read();
}

// ============================================================================
// sentinel::test::ds3231::presence_check
// ============================================================================

void sentinel::test::ds3231::presence_check() {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 presence_check: driver constructed", "");
    yield_for_debug_drain(200);

    auto status = rtc.status();
    if (!status) {
        loge("presence_check FAIL: status read error %d",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 presence_check FAIL: status read error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
    }

    // A floating I²C bus typically reads back 0xFF for every byte (pullups
    // with no slave asserting). The DS3231 status register has reserved
    // bits at positions 4..6 that always read 0, so 0xFF is impossible
    // from a working part.
    if (*status == 0xFF) {
        loge("presence_check FAIL: status=0xFF (no slave?)", "");
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 presence_check FAIL: status=0xFF\n");
        return;
    }

    auto osf = rtc.oscillator_stop_flag();
    if (!osf) {
        loge("presence_check FAIL: OSF read error %d",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 presence_check FAIL: OSF read error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
    }

    logi("presence_check PASS: status=0x%02X OSF=%d", static_cast<int>(*status),
         *osf ? 1 : 0);
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "DS3231 presence_check PASS: status=0x%02X OSF=%d\n",
               static_cast<int>(*status), *osf ? 1 : 0);

    if (*osf) {
        logw(
            "presence_check: oscillator-stop flag set; time may be "
            "uninitialized. Call set_time() then clear_oscillator_stop_flag().",
            "");
    }
}

// ============================================================================
// sentinel::test::ds3231::register_round_trip
// ============================================================================

void sentinel::test::ds3231::register_round_trip() {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 register_round_trip: driver constructed", "");
    yield_for_debug_drain(200);

    // Snapshot original aging-offset so we can restore it.
    auto original = rtc.aging_offset();
    if (!original) {
        loge("register_round_trip FAIL: initial read error %d",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 register_round_trip FAIL: initial read error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
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
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 register_round_trip FAIL: write error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
    }

    auto readback = rtc.aging_offset();
    if (!readback) {
        loge("register_round_trip FAIL: readback error %d",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 register_round_trip FAIL: readback error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
    }

    if (*readback != target) {
        loge("register_round_trip FAIL: readback %d != expected %d",
             static_cast<int>(*readback), static_cast<int>(target));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 register_round_trip FAIL: readback %d != %d\n",
                   static_cast<int>(*readback), static_cast<int>(target));
    } else {
        logi("register_round_trip PASS: aging_offset readback=%d",
             static_cast<int>(*readback));
        cy_log_msg(
            CYLF_DEF, CY_LOG_INFO,
            "DS3231 register_round_trip PASS: aging_offset readback=%d\n",
            static_cast<int>(*readback));
    }

    // Restore original — best-effort; a failure here is logged but does
    // not affect the test result.
    if (!rtc.set_aging_offset(*original)) {
        logw("register_round_trip: restore error %d",
             static_cast<int>(rtc.last_error()));
    }
}

// ============================================================================
// sentinel::test::ds3231::time_read
// ============================================================================

void sentinel::test::ds3231::time_read() {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 time_read: driver constructed", "");
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
            cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                       "DS3231 time_read FAIL: read %d error %d\n",
                       static_cast<int>(i), static_cast<int>(rtc.last_error()));
            return;
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
        logi("time_read PASS: seconds field changed across reads", "");
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "DS3231 time_read PASS: oscillator is running\n");
    } else {
        logw("time_read: seconds did not change across 1s of reads — "
             "could be coincidence at boundary, or oscillator stopped",
             "");
        cy_log_msg(CYLF_DEF, CY_LOG_WARNING,
                   "DS3231 time_read WARN: seconds unchanged (boundary?)\n");
    }
}

// ============================================================================
// sentinel::test::ds3231::time_write
// ============================================================================

void sentinel::test::ds3231::time_write() {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 time_write: driver constructed", "");
    yield_for_debug_drain(200);

    // Snapshot the pre-write time for the log; we do not restore.
    auto before = rtc.time();
    if (!before) {
        loge("time_write FAIL: initial read error %d",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 time_write FAIL: initial read error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
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
    target.year        = 2024;
    target.month       = 2;
    target.date        = 29;
    target.day_of_week = static_cast<uint8_t>(ds3231_t::day_of_week::thursday);
    target.hour        = 12;
    target.minute      = 34;
    target.second      = 56;

    if (!rtc.set_time(target)) {
        loge("time_write FAIL: set_time error %d",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 time_write FAIL: set_time error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
    }

    // Read back. A few hundred microseconds of I²C round-trip mean the
    // seconds field could have advanced by up to 1 in practice; accept
    // anything in [target.second, target.second + 2] modulo 60 to be safe.
    auto after = rtc.time();
    if (!after) {
        loge("time_write FAIL: readback error %d",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 time_write FAIL: readback error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
    }

    auto seconds_drift_ok = false;
    for (auto offset = uint8_t{0}; offset <= 2; ++offset) {
        auto expected_second = static_cast<uint8_t>(
            (target.second + offset) % 60);
        if (after->second == expected_second) {
            seconds_drift_ok = true;
            break;
        }
    }

    auto fields_ok = after->year        == target.year
                  && after->month       == target.month
                  && after->date        == target.date
                  && after->day_of_week == target.day_of_week
                  && after->hour        == target.hour
                  && after->minute      == target.minute
                  && seconds_drift_ok;

    if (!fields_ok) {
        loge("time_write FAIL: readback mismatch — "
             "wrote %04d-%02d-%02d %s %02d:%02d:%02d, "
             "got %04d-%02d-%02d %s %02d:%02d:%02d",
             static_cast<int>(target.year), static_cast<int>(target.month),
             static_cast<int>(target.date), day_name(target.day_of_week),
             static_cast<int>(target.hour), static_cast<int>(target.minute),
             static_cast<int>(target.second),
             static_cast<int>(after->year), static_cast<int>(after->month),
             static_cast<int>(after->date), day_name(after->day_of_week),
             static_cast<int>(after->hour), static_cast<int>(after->minute),
             static_cast<int>(after->second));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 time_write FAIL: readback mismatch\n");
        return;
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
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "DS3231 time_write PASS: wrote 2024-02-29 12:34:56 Thu, "
               "readback %02d:%02d:%02d (OSF cleared, time NOT restored)\n",
               static_cast<int>(after->hour), static_cast<int>(after->minute),
               static_cast<int>(after->second));
}

// ============================================================================
// sentinel::test::ds3231::time_sync_from_build
// ============================================================================

void sentinel::test::ds3231::time_sync_from_build() {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 time_sync_from_build: driver constructed", "");
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
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 time_sync_from_build FAIL: sync error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
    }

    auto after = rtc.time();
    if (!after) {
        loge("time_sync_from_build FAIL: post-sync read error %d",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 time_sync_from_build FAIL: read error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
    }

    // Sanity-check the readback against the captured build year/month/date.
    // We don't compare hours/minutes/seconds because by the time we read
    // back, real-world wall time has likely advanced past what the helper
    // wrote (fudge + I²C round-trip + extra logging).
    auto date_matches = after->year  == sentinel::build_time::build_year()
                     && after->month == sentinel::build_time::build_month()
                     && after->date  == sentinel::build_time::build_day();
    if (!date_matches) {
        logw("time_sync_from_build: post-sync date %04d-%02d-%02d "
             "differs from build date %04d-%02d-%02d "
             "(boundary crossed during sync? non-fatal)",
             static_cast<int>(after->year),
             static_cast<int>(after->month),
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
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "DS3231 time_sync_from_build PASS: RTC now "
               "%04d-%02d-%02d %s %02d:%02d:%02d\n",
               static_cast<int>(after->year), static_cast<int>(after->month),
               static_cast<int>(after->date), day_name(after->day_of_week),
               static_cast<int>(after->hour), static_cast<int>(after->minute),
               static_cast<int>(after->second));
}

// ============================================================================
// sentinel::test::ds3231::temperature_read
// ============================================================================

void sentinel::test::ds3231::temperature_read() {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 temperature_read: driver constructed", "");
    yield_for_debug_drain(200);

    // Read whatever the last scheduled conversion left in the registers.
    auto first = rtc.temperature_centi_c();
    if (!first) {
        loge("temperature_read FAIL: initial read error %d",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 temperature_read FAIL: initial read error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
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
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 temperature_read FAIL: start_conversion error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
    }
    logi("temperature_read: CONV set, polling BSY...", "");

    // Poll BSY for up to ~200 ms. The datasheet quotes typical conversion
    // time at ~125 ms.
    auto completed = false;
    for (auto i = uint8_t{0}; i < 20; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
        auto busy = rtc.is_temperature_conversion_busy();
        if (!busy) {
            loge("temperature_read FAIL: BSY poll error %d",
                 static_cast<int>(rtc.last_error()));
            cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                       "DS3231 temperature_read FAIL: BSY poll error %d\n",
                       static_cast<int>(rtc.last_error()));
            return;
        }
        if (!*busy) {
            completed = true;
            break;
        }
    }

    if (!completed) {
        loge("temperature_read FAIL: BSY did not clear within 200ms", "");
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 temperature_read FAIL: BSY timeout\n");
        return;
    }

    auto second = rtc.temperature_centi_c();
    if (!second) {
        loge("temperature_read FAIL: post-conversion read error %d",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 temperature_read FAIL: post-conv read error %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
    }
    auto sign_b = char{};
    auto whole_b = int32_t{};
    auto frac_b = int32_t{};
    split_centi(*second, sign_b, whole_b, frac_b);
    logi("temperature_read[post-conv]: %c%d.%02d C", sign_b,
         static_cast<int>(whole_b), static_cast<int>(frac_b));

    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "DS3231 temperature_read PASS: %c%d.%02d C\n", sign_b,
               static_cast<int>(whole_b), static_cast<int>(frac_b));
}

// ============================================================================
// sentinel::test::ds3231::alarm_round_trip
// ============================================================================

void sentinel::test::ds3231::alarm_round_trip() {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 alarm_round_trip: driver constructed", "");
    yield_for_debug_drain(200);

    // Keep alarm interrupts off across the test so the INT/SQW pin
    // does not unexpectedly assert while we are scribbling on the
    // alarm registers.
    if (!rtc.set_alarm1_interrupt_enabled(false) ||
        !rtc.set_alarm2_interrupt_enabled(false)) {
        loge("alarm_round_trip FAIL: could not disable alarm interrupts "
             "(last_err=%d)",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 alarm_round_trip FAIL: disable IE %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
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
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 alarm_round_trip FAIL: set_alarm1 %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
    }

    auto alarm1_readback = rtc.alarm1();
    if (!alarm1_readback) {
        loge("alarm_round_trip FAIL: get_alarm1 error %d",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 alarm_round_trip FAIL: get_alarm1 %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
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
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 alarm_round_trip FAIL: alarm1 mismatch\n");
        return;
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
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 alarm_round_trip FAIL: set_alarm2 %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
    }

    auto alarm2_readback = rtc.alarm2();
    if (!alarm2_readback) {
        loge("alarm_round_trip FAIL: get_alarm2 error %d",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 alarm_round_trip FAIL: get_alarm2 %d\n",
                   static_cast<int>(rtc.last_error()));
        return;
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
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "DS3231 alarm_round_trip FAIL: alarm2 mismatch\n");
        return;
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

    cy_log_msg(CYLF_DEF, CY_LOG_INFO, "DS3231 alarm_round_trip PASS\n");
}

// ============================================================================
// sentinel::test::ds3231::continuous_read
// ============================================================================

[[noreturn]] void sentinel::test::ds3231::continuous_read() {
    auto rtc = ds3231_t(ds3231_bus);
    logi("DS3231 continuous_read: entering 1 Hz loop", "");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "DS3231 continuous_read: entering 1 Hz loop\n");
    yield_for_debug_drain(200);

    while (true) {
        auto now = rtc.time();
        auto temp = rtc.temperature_centi_c();

        if (!now) {
            loge("continuous_read: time error %d",
                 static_cast<int>(rtc.last_error()));
            cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                       "DS3231 continuous_read: time error %d\n",
                       static_cast<int>(rtc.last_error()));
        } else if (!temp) {
            loge("continuous_read: temperature error %d",
                 static_cast<int>(rtc.last_error()));
            cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                       "DS3231 continuous_read: temperature error %d\n",
                       static_cast<int>(rtc.last_error()));
        } else {
            auto sign = char{};
            auto whole = int32_t{};
            auto frac = int32_t{};
            split_centi(*temp, sign, whole, frac);

            logi("%04d-%02d-%02d %s %02d:%02d:%02d  T=%c%d.%02d C",
                 static_cast<int>(now->year), static_cast<int>(now->month),
                 static_cast<int>(now->date), day_name(now->day_of_week),
                 static_cast<int>(now->hour), static_cast<int>(now->minute),
                 static_cast<int>(now->second), sign, static_cast<int>(whole),
                 static_cast<int>(frac));

            cy_log_msg(
                CYLF_DEF, CY_LOG_INFO,
                "DS3231 %04d-%02d-%02d %s %02d:%02d:%02d  "
                "T=%c%d.%02d C\n",
                static_cast<int>(now->year), static_cast<int>(now->month),
                static_cast<int>(now->date), day_name(now->day_of_week),
                static_cast<int>(now->hour), static_cast<int>(now->minute),
                static_cast<int>(now->second), sign, static_cast<int>(whole),
                static_cast<int>(frac));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================================
// sentinel::test::ds3231::task_create
// ============================================================================

BaseType_t sentinel::test::ds3231::task_create() {
    constexpr auto stack_words = configMINIMAL_STACK_SIZE * 4;
    constexpr auto priority =
        static_cast<UBaseType_t>(configMAX_PRIORITIES - 3);

    return xTaskCreate([](void *) -> void { sentinel::test::ds3231::all(); },
                       "DS3231 Test Task", stack_words, nullptr, priority,
                       nullptr);
}
