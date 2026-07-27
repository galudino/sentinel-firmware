///
/// \file    sentinel_test_bme280.cpp
/// \brief   BME280 sensor driver test suite implementation
///
/// \details Implements the run-to-completion testbench suite declared in
///          \c sentinel_test_bme280.hpp. The tests exercise the public members
///          of \ref sentinel::bme280 against a physical BME280 attached to
///          \c sentinel::resource::cybsp_i2c_bus (the primary I²C bus exposed
///          by Device Configurator).
///
///          API style: the tests use the modern \c std::optional / \c bool
///          public surface of \ref sentinel::bme280, with \c last_error()
///          inspected on failure to recover the raw Bosch error code for
///          logging.
///
///          Structure (#48): the individual tests are members of a TU-local
///          \c fixture that owns the bus-arbitrated transport, mirroring a
///          GoogleTest \c TEST_F fixture — the shared resource lives in the
///          fixture, not a file-static global. Each test returns \c true on
///          pass / \c false on fail; \ref sentinel::test::bme280::run_all
///          constructs the fixture, folds every outcome into a
///          \ref sentinel::test::tally, and returns it.
///
///          Output strategy:
///          - Progress and PASS / FAIL summary lines are emitted via
///            \c logi / \c loge; the logging facade (#50) writes each line once
///            to both the retarget-IO UART serial monitor and the BLE
///            debug-stream view in SentinelPanel.
///
///          Floating-point handling: the Bosch BME280 driver is compiled in
///          double-precision compensation mode. Per the project-wide constraint
///          in \c sentinel_debug_print.hpp, the test never formats floats with
///          \c %f / \c %e / \c %g — every sample is scaled to integers before
///          printing.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 2.0 - Run-to-completion fixture suite (#48)
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "bme280.h"
#include "bme280_defs.h"
#include "cy_result.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_bme280.hpp"
#include "sentinel_cyhal_i2c_bus_transport.hpp"
#include "sentinel_debug_print.hpp"
#include "sentinel_resource.hpp"
#include "sentinel_test_bme280.hpp"
#include "sentinel_test_result.hpp"
#include "sentinel_utilities.hpp"

#include <cstdint>
#include <optional>

namespace {

///
/// \brief Yield long enough for the BLE debug ring buffer to drain
///
/// \details The debug stream's ring buffer is best-effort; rapid back-to-back
///          \c logi() calls can overflow it and silently drop messages.
///          Inserting a short \c vTaskDelay between logical test phases
///          gives the debug-stream task time to push pending bytes out as
///          BLE notifications before the next batch arrives.
///
/// \param   milliseconds Yield duration in milliseconds.
///
inline void yield_for_debug_drain(uint32_t milliseconds) noexcept {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

///
/// \brief Test fixture: owns the bus-arbitrated transport every test shares.
///
/// \details Routes through \c sentinel::resource::cybsp_i2c_bus (the FreeRTOS
///          bus-arbiter task) so this suite's transactions serialize cleanly
///          with any other tasks sharing the same physical I²C bus — notably
///          the DS3231 suite. Constructed fresh by
///          \ref sentinel::test::bme280::run_all (like a
///          GoogleTest \c SetUp), so there is no file-static bus global. The
///          transport is inert until \c peripheral_initialize() has spawned the
///          arbiter, which the orchestrator guarantees by running
///          post-scheduler.
///
struct fixture {
    /// Bus-arbitrated I2C transport, shared by every test below.
    sentinel::cyhal_i2c_bus_transport bme280_bus{
        sentinel::resource::cybsp_i2c_bus, BME280_I2C_ADDR_PRIM};

    /// \brief Read the chip ID and confirm it matches \c BME280_CHIP_ID.
    /// \return \c true on match; \c false on transport error or mismatch.
    bool chip_id_read() noexcept;
    /// \brief Soft-reset the sensor and confirm the chip ID re-reads clean.
    /// \return \c true if the post-reset chip ID matches; \c false otherwise.
    bool soft_reset() noexcept;
    /// \brief Mutate the filter setting, write it back, and read it back.
    /// \return \c true if the readback matches the mutated value.
    bool settings_round_trip() noexcept;
    /// \brief Exercise SLEEP -> FORCED -> SLEEP power-mode transitions.
    /// \return \c true if each observed mode is one of the expected values.
    bool sensor_mode_transitions() noexcept;
};

} // namespace

// ============================================================================
// fixture::chip_id_read
// ============================================================================

bool fixture::chip_id_read() noexcept {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_bus_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("BME280 chip_id_read: sensor init OK");
    yield_for_debug_drain(200);

    auto id = sensor.read_chip_id();

    if (!id) {
        loge("chip_id_read FAIL: I2C error %d",
             static_cast<int>(sensor.last_error()));
        return false;
    }

    if (*id == BME280_CHIP_ID) {
        logi("chip_id_read PASS: 0x%02X (%d)", static_cast<int>(*id),
             static_cast<int>(*id));
        return true;
    }

    loge("chip_id_read FAIL: got 0x%02X, expected 0x%02X",
         static_cast<int>(*id), static_cast<int>(BME280_CHIP_ID));
    return false;
}

// ============================================================================
// fixture::soft_reset
// ============================================================================

bool fixture::soft_reset() noexcept {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_bus_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("soft_reset: sensor init OK");
    yield_for_debug_drain(200);

    if (!sensor.soft_reset()) {
        loge("soft_reset FAIL: reset call error %d",
             static_cast<int>(sensor.last_error()));
        return false;
    }

    // Datasheet specifies a 2 ms startup delay after reset before the part
    // is ready to accept further commands.
    yield_for_debug_drain(5);

    // Confirm the bus is still good and the part still answers with the
    // correct chip ID after the reset.
    auto id = sensor.read_chip_id();

    if (!id || *id != BME280_CHIP_ID) {
        loge("soft_reset FAIL: post-reset chip ID 0x%02X (last_err=%d)",
             id ? static_cast<int>(*id) : 0xFF,
             static_cast<int>(sensor.last_error()));
        return false;
    }

    logi("soft_reset PASS: chip ID 0x%02X after reset", static_cast<int>(*id));
    return true;
}

// ============================================================================
// fixture::settings_round_trip
// ============================================================================

bool fixture::settings_round_trip() noexcept {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_bus_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("settings_round_trip: sensor init OK");
    yield_for_debug_drain(200);

    // Snapshot the device's current settings so we can restore them.
    auto original = sensor.sensor_settings();
    if (!original) {
        loge("settings_round_trip FAIL: initial get error %d",
             static_cast<int>(sensor.last_error()));
        return false;
    }

    logi("settings_round_trip: original filter=%d osr_t=%d osr_p=%d osr_h=%d",
         static_cast<int>(original->filter), static_cast<int>(original->osr_t),
         static_cast<int>(original->osr_p), static_cast<int>(original->osr_h));
    yield_for_debug_drain(200);

    // Mutate the filter coefficient to a value distinct from the driver's
    // default (which is FILTER_COEFF_16). The check below tolerates either
    // outcome — we just need the device to round-trip *some* observable
    // change.
    auto mutated = *original;
    auto target_filter = (original->filter == BME280_FILTER_COEFF_8)
                             ? BME280_FILTER_COEFF_4
                             : BME280_FILTER_COEFF_8;
    mutated.filter = target_filter;

    if (!sensor.set_sensor_settings(BME280_SEL_FILTER, mutated)) {
        loge("settings_round_trip FAIL: set error %d",
             static_cast<int>(sensor.last_error()));
        return false;
    }

    // Read back and confirm.
    auto readback = sensor.sensor_settings();
    if (!readback) {
        loge("settings_round_trip FAIL: readback error %d",
             static_cast<int>(sensor.last_error()));
        return false;
    }

    auto ok = bool{};
    if (readback->filter != target_filter) {
        loge("settings_round_trip FAIL: filter %d != expected %d",
             static_cast<int>(readback->filter),
             static_cast<int>(target_filter));
        ok = false;
    } else {
        logi("settings_round_trip PASS: filter %d",
             static_cast<int>(readback->filter));
        ok = true;
    }

    // Restore original settings — best-effort; a failure here is logged but
    // does not affect the test result.
    if (!sensor.set_sensor_settings(BME280_SEL_FILTER, *original)) {
        logw("settings_round_trip: restore error %d",
             static_cast<int>(sensor.last_error()));
    }

    return ok;
}

// ============================================================================
// fixture::sensor_mode_transitions
// ============================================================================

bool fixture::sensor_mode_transitions() noexcept {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_bus_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("sensor_mode_transitions: sensor init OK");
    yield_for_debug_drain(200);

    // Park the part in sleep first; this gives us a known starting state
    // regardless of what configure_sensor() left programmed in the ctrl
    // registers.
    if (!sensor.set_sensor_mode(BME280_POWERMODE_SLEEP)) {
        loge("sensor_mode_transitions FAIL: set SLEEP error %d",
             static_cast<int>(sensor.last_error()));
        return false;
    }

    auto mode = sensor.sensor_mode();
    if (!mode || *mode != BME280_POWERMODE_SLEEP) {
        loge("sensor_mode_transitions FAIL: expected SLEEP, got %d "
             "(last_err=%d)",
             mode ? static_cast<int>(*mode) : -1,
             static_cast<int>(sensor.last_error()));
        return false;
    }
    logi("sensor_mode_transitions: SLEEP confirmed (%d)",
         static_cast<int>(*mode));
    yield_for_debug_drain(100);

    // FORCED is self-clearing: the BME280 returns to SLEEP automatically
    // once the one-shot measurement completes. We sample the mode register
    // immediately after the transition to maximize the chance of catching
    // it before the auto-return.
    if (!sensor.set_sensor_mode(BME280_POWERMODE_FORCED)) {
        loge("sensor_mode_transitions FAIL: set FORCED error %d",
             static_cast<int>(sensor.last_error()));
        return false;
    }

    mode = sensor.sensor_mode();
    if (!mode) {
        loge("sensor_mode_transitions FAIL: FORCED readback error %d",
             static_cast<int>(sensor.last_error()));
        return false;
    }

    // Accept either FORCED or SLEEP — the conversion may have already
    // completed by the time we read back.
    if (*mode != BME280_POWERMODE_FORCED && *mode != BME280_POWERMODE_SLEEP) {
        loge("sensor_mode_transitions FAIL: unexpected mode %d after FORCED",
             static_cast<int>(*mode));
        return false;
    }

    logi("sensor_mode_transitions PASS: post-FORCED mode %d",
         static_cast<int>(*mode));
    return true;
}

// ============================================================================
// sentinel::test::bme280::run_all
// ============================================================================

sentinel::test::tally sentinel::test::bme280::run_all() noexcept {
    auto fx = fixture{};
    auto t = sentinel::test::tally{};

    t.record(fx.chip_id_read());
    yield_for_debug_drain(200);

    t.record(fx.soft_reset());
    yield_for_debug_drain(200);

    t.record(fx.settings_round_trip());
    yield_for_debug_drain(200);

    t.record(fx.sensor_mode_transitions());
    yield_for_debug_drain(200);

    return t;
}
