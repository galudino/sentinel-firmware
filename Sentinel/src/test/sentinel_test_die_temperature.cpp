///
/// \file    sentinel_test_die_temperature.cpp
/// \brief   On-target test suite for the PSoC 6 die-temperature driver (#55)
///
/// \author  galudino
/// \date    2026-07-09
/// \version 1.0
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

#include "sentinel_debug_print.hpp"
#include "sentinel_psoc6_die_temperature.hpp"
#include "sentinel_test_die_temperature.hpp"

#include <cstdint>

namespace {

/// \brief Plausible die-temperature window, in 0.01 °C. Wider than a lab bench
///        (to avoid false failures in a cold/warm room) but tight enough to
///        catch a broken conversion (e.g. a raw-counts or sign error).
constexpr int16_t PLAUSIBLE_MIN_CENTI_C = -4000; // -40 °C (operating minimum)
constexpr int16_t PLAUSIBLE_MAX_CENTI_C = 12500; // 125 °C (operating maximum)

/// \brief Max spread across a short burst of readings (0.01 °C). The die cannot
///        physically swing this far in a few hundred milliseconds; a larger
///        spread indicates noise / a config problem.
constexpr int32_t MAX_BURST_SPREAD_CENTI_C = 1500; // 15 °C

void yield_for_debug_drain(uint32_t ms) noexcept {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/// \brief Force a fresh reading past the driver's ~1 s refresh throttle.
bool read_fresh(int16_t &out_centi) noexcept {
    auto &die = sentinel::drivers::psoc6_die_temperature::instance();
    // Space the calls beyond the throttle window so refresh() actually converts.
    for (int attempt = 0; attempt < 3; ++attempt) {
        die.refresh();
        if (die.cached_centi_c(out_centi)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1100));
    }
    return false;
}

bool test_initialize() noexcept {
    const bool ok =
        sentinel::drivers::psoc6_die_temperature::instance().initialize();
    if (!ok) {
        loge("die_temperature initialize FAIL");
        return false;
    }
    logi("die_temperature initialize PASS");
    return true;
}

bool test_reading_available() noexcept {
    int16_t centi = 0;
    if (!read_fresh(centi)) {
        loge("die_temperature reading_available FAIL: no reading produced");
        return false;
    }
    logi("die_temperature reading_available PASS: %d (0.01 C)",
         static_cast<int>(centi));
    return true;
}

bool test_plausible_range() noexcept {
    int16_t centi = 0;
    if (!read_fresh(centi)) {
        loge("die_temperature plausible_range FAIL: no reading");
        return false;
    }
    if (centi < PLAUSIBLE_MIN_CENTI_C || centi > PLAUSIBLE_MAX_CENTI_C) {
        loge("die_temperature plausible_range FAIL: %d out of [%d, %d]",
             static_cast<int>(centi), static_cast<int>(PLAUSIBLE_MIN_CENTI_C),
             static_cast<int>(PLAUSIBLE_MAX_CENTI_C));
        return false;
    }
    logi("die_temperature plausible_range PASS: %d.%02d C",
         static_cast<int>(centi / 100),
         static_cast<int>((centi < 0 ? -centi : centi) % 100));
    return true;
}

bool test_stability() noexcept {
    int16_t min_c = 0;
    int16_t max_c = 0;
    bool have_any = false;

    for (int i = 0; i < 4; ++i) {
        int16_t centi = 0;
        if (!read_fresh(centi)) {
            loge("die_temperature stability FAIL: read %d failed", i);
            return false;
        }
        if (!have_any) {
            min_c = max_c = centi;
            have_any = true;
        } else {
            if (centi < min_c) {
                min_c = centi;
            }
            if (centi > max_c) {
                max_c = centi;
            }
        }
    }

    const int32_t spread = static_cast<int32_t>(max_c) - min_c;
    if (spread > MAX_BURST_SPREAD_CENTI_C) {
        loge("die_temperature stability FAIL: spread %ld (0.01 C) too large",
             static_cast<long>(spread));
        return false;
    }
    logi("die_temperature stability PASS: spread %ld (0.01 C)",
         static_cast<long>(spread));
    return true;
}

} // namespace

sentinel::test::tally sentinel::test::die_temperature::run_all() noexcept {
    auto t = sentinel::test::tally{};

    const bool init_ok = test_initialize();
    t.record(init_ok);
    yield_for_debug_drain(200);

    if (!init_ok) {
        // Without a live SAR the remaining reads cannot succeed; report them as
        // failures rather than crash-looping on an uninitialized sensor.
        loge("die_temperature: skipping reads — sensor not initialized");
        t.record(false);
        t.record(false);
        t.record(false);
        return t;
    }

    t.record(test_reading_available());
    yield_for_debug_drain(200);

    t.record(test_plausible_range());
    yield_for_debug_drain(200);

    t.record(test_stability());
    yield_for_debug_drain(200);

    return t;
}
