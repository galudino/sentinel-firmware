///
/// \file    sentinel_test_bme280.cpp
/// \brief   BME280 sensor driver test implementations
///
/// \details Implements the testbench smoke tests declared in
///          \c sentinel_test_bme280.hpp. The tests exercise every public
///          member of \ref sentinel::bme280 against a physical BME280
///          attached to \c sentinel::resource::cybsp_i2c (the primary I²C
///          bus exposed by Device Configurator).
///
///          API style: the tests use the modern \c std::optional / \c bool
///          public surface of \ref sentinel::bme280, with \c last_error()
///          inspected on failure to recover the raw Bosch error code for
///          logging.
///
///          Output strategy:
///          - Step-by-step progress is emitted via \c logi / \c loge so it
///            appears in the BLE debug-stream view in SentinelPanel.
///          - PASS / FAIL summary lines are *also* emitted via
///            \c cy_log_msg so they are visible on the retarget-IO UART
///            even if the BLE debug-stream ring buffer overflows or no
///            BLE central is connected.
///
///          Floating-point handling: the Bosch BME280 driver is compiled in
///          double-precision compensation mode (the default when none of
///          \c BME280_DOUBLE_ENABLE / \c BME280_32BIT_ENABLE /
///          \c BME280_64BIT_ENABLE is overridden). Per the project-wide
///          constraint in \c sentinel_debug_print.hpp, the test never
///          formats floats with \c %f / \c %e / \c %g — every sample is
///          scaled to integers before printing.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.1 - Aligned with optional/bool driver API
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "bme280.h"
#include "bme280_defs.h"
#include "cy_log.h"
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
#include "sentinel_utilities.hpp"

#include <cstdint>
#include <optional>

namespace {

///
/// \brief Bus-arbitrated transport instance used by every BME280 test.
///
/// \details Routes through \c sentinel::resource::cybsp_i2c_bus (the
///          FreeRTOS bus-arbiter task) so this test's transactions
///          serialize cleanly with any other tasks sharing the same
///          physical I²C bus — notably the DS3231 test. Storage lives in
///          BSS until \c peripheral_initialize() spawns the arbiter; the
///          transport itself is inert until the testbench has finished
///          hardware init.
///
sentinel::cyhal_i2c_bus_transport
    bme280_bus(sentinel::resource::cybsp_i2c_bus, BME280_I2C_ADDR_PRIM);

///
/// \brief Yield long enough for the BLE debug ring buffer to drain
///
/// \details The debug stream's ring buffer is 256 bytes; rapid back-to-back
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
/// \brief Format a signed centi-unit integer with two decimal places
///
/// \details Helper that pulls the sign apart from the magnitude so the
///          \c %d.%02d trick does not produce strings like \c "-23.-05".
///          Used by \ref sentinel::test::bme280::continuous_read.
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

} // namespace

// ============================================================================
// sentinel::test::bme280::all
// ============================================================================

void sentinel::test::bme280::all() {
    chip_id_read();
    yield_for_debug_drain(200);

    soft_reset();
    yield_for_debug_drain(200);

    settings_round_trip();
    yield_for_debug_drain(200);

    sensor_mode_transitions();
    yield_for_debug_drain(200);

    // Never returns — runs forever at 1 Hz.
    continuous_read();
}

// ============================================================================
// sentinel::test::bme280::chip_id_read
// ============================================================================

void sentinel::test::bme280::chip_id_read() {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_bus_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("BME280 chip_id_read: sensor init OK", "");
    yield_for_debug_drain(200);

    auto id = sensor.read_chip_id();

    if (!id) {
        loge("chip_id_read FAIL: I2C error %d",
             static_cast<int>(sensor.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 chip_id_read FAIL: I2C error %d\n",
                   static_cast<int>(sensor.last_error()));
        return;
    }

    if (*id == BME280_CHIP_ID) {
        logi("chip_id_read PASS: 0x%02X (%d)", static_cast<int>(*id),
             static_cast<int>(*id));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO, "BME280 chip_id_read PASS: 0x%02X\n",
                   static_cast<int>(*id));
        return;
    }

    loge("chip_id_read FAIL: got 0x%02X, expected 0x%02X",
         static_cast<int>(*id), static_cast<int>(BME280_CHIP_ID));
    cy_log_msg(CYLF_DEF, CY_LOG_ERR,
               "BME280 chip_id_read FAIL: got 0x%02X, expected 0x%02X\n",
               static_cast<int>(*id), static_cast<int>(BME280_CHIP_ID));
}

// ============================================================================
// sentinel::test::bme280::soft_reset
// ============================================================================

void sentinel::test::bme280::soft_reset() {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_bus_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("soft_reset: sensor init OK", "");
    yield_for_debug_drain(200);

    if (!sensor.soft_reset()) {
        loge("soft_reset FAIL: reset call error %d",
             static_cast<int>(sensor.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR, "BME280 soft_reset FAIL: %d\n",
                   static_cast<int>(sensor.last_error()));
        return;
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
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 soft_reset FAIL: post-reset chip ID 0x%02X\n",
                   id ? static_cast<int>(*id) : 0xFF);
        return;
    }

    logi("soft_reset PASS: chip ID 0x%02X after reset", static_cast<int>(*id));
    cy_log_msg(CYLF_DEF, CY_LOG_INFO, "BME280 soft_reset PASS\n");
}

// ============================================================================
// sentinel::test::bme280::settings_round_trip
// ============================================================================

void sentinel::test::bme280::settings_round_trip() {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_bus_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("settings_round_trip: sensor init OK", "");
    yield_for_debug_drain(200);

    // Snapshot the device's current settings so we can restore them.
    auto original = sensor.sensor_settings();
    if (!original) {
        loge("settings_round_trip FAIL: initial get error %d",
             static_cast<int>(sensor.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 settings_round_trip FAIL: initial get %d\n",
                   static_cast<int>(sensor.last_error()));
        return;
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
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 settings_round_trip FAIL: set %d\n",
                   static_cast<int>(sensor.last_error()));
        return;
    }

    // Read back and confirm.
    auto readback = sensor.sensor_settings();
    if (!readback) {
        loge("settings_round_trip FAIL: readback error %d",
             static_cast<int>(sensor.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 settings_round_trip FAIL: readback %d\n",
                   static_cast<int>(sensor.last_error()));
        return;
    }

    if (readback->filter != target_filter) {
        loge("settings_round_trip FAIL: filter %d != expected %d",
             static_cast<int>(readback->filter),
             static_cast<int>(target_filter));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 settings_round_trip FAIL: filter %d != %d\n",
                   static_cast<int>(readback->filter),
                   static_cast<int>(target_filter));
    } else {
        logi("settings_round_trip PASS: filter %d",
             static_cast<int>(readback->filter));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "BME280 settings_round_trip PASS: filter %d\n",
                   static_cast<int>(readback->filter));
    }

    // Restore original settings — best-effort; a failure here is logged but
    // does not affect the test result.
    if (!sensor.set_sensor_settings(BME280_SEL_FILTER, *original)) {
        logw("settings_round_trip: restore error %d",
             static_cast<int>(sensor.last_error()));
    }
}

// ============================================================================
// sentinel::test::bme280::sensor_mode_transitions
// ============================================================================

void sentinel::test::bme280::sensor_mode_transitions() {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_bus_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("sensor_mode_transitions: sensor init OK", "");
    yield_for_debug_drain(200);

    // Park the part in sleep first; this gives us a known starting state
    // regardless of what configure_sensor() left programmed in the ctrl
    // registers.
    if (!sensor.set_sensor_mode(BME280_POWERMODE_SLEEP)) {
        loge("sensor_mode_transitions FAIL: set SLEEP error %d",
             static_cast<int>(sensor.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 sensor_mode_transitions FAIL: set SLEEP %d\n",
                   static_cast<int>(sensor.last_error()));
        return;
    }

    auto mode = sensor.sensor_mode();
    if (!mode || *mode != BME280_POWERMODE_SLEEP) {
        loge("sensor_mode_transitions FAIL: expected SLEEP, got %d "
             "(last_err=%d)",
             mode ? static_cast<int>(*mode) : -1,
             static_cast<int>(sensor.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 sensor_mode_transitions FAIL: SLEEP readback %d\n",
                   mode ? static_cast<int>(*mode) : -1);
        return;
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
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 sensor_mode_transitions FAIL: set FORCED %d\n",
                   static_cast<int>(sensor.last_error()));
        return;
    }

    mode = sensor.sensor_mode();
    if (!mode) {
        loge("sensor_mode_transitions FAIL: FORCED readback error %d",
             static_cast<int>(sensor.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 sensor_mode_transitions FAIL: FORCED readback %d\n",
                   static_cast<int>(sensor.last_error()));
        return;
    }

    // Accept either FORCED or SLEEP — the conversion may have already
    // completed by the time we read back.
    if (*mode != BME280_POWERMODE_FORCED && *mode != BME280_POWERMODE_SLEEP) {
        loge("sensor_mode_transitions FAIL: unexpected mode %d after FORCED",
             static_cast<int>(*mode));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 sensor_mode_transitions FAIL: post-FORCED mode %d\n",
                   static_cast<int>(*mode));
        return;
    }

    logi("sensor_mode_transitions PASS: post-FORCED mode %d",
         static_cast<int>(*mode));
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "BME280 sensor_mode_transitions PASS: post-FORCED mode %d\n",
               static_cast<int>(*mode));
}

// ============================================================================
// sentinel::test::bme280::continuous_read
// ============================================================================

[[noreturn]] void sentinel::test::bme280::continuous_read() {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_bus_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("continuous_read: sensor init OK", "");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "BME280 continuous_read: entering 1 Hz loop\n");
    yield_for_debug_drain(200);

    // Gate-check: never enter the loop if the chip ID is wrong.
    auto id = sensor.read_chip_id();

    if (!id || *id != BME280_CHIP_ID) {
        loge("continuous_read ABORT: chip ID 0x%02X (last_err=%d)",
             id ? static_cast<int>(*id) : 0xFF,
             static_cast<int>(sensor.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 continuous_read ABORT: chip ID 0x%02X\n",
                   id ? static_cast<int>(*id) : 0xFF);
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    logi("continuous_read: chip ID OK (0x%02X)", static_cast<int>(*id));
    yield_for_debug_drain(200);

    while (true) {
        auto sample = sensor.read_sensor_data();

        if (!sample) {
            loge("continuous_read: read error %d",
                 static_cast<int>(sensor.last_error()));
            cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                       "BME280 continuous_read: read error %d\n",
                       static_cast<int>(sensor.last_error()));
        } else {
            // Bosch driver is compiled in BME280_DOUBLE_ENABLE mode by
            // default — pressure/temperature/humidity are doubles in Pa,
            // °C, and %RH respectively. Scale to integers so we never
            // invoke a %f formatter.
            auto temp_centi = static_cast<int32_t>(sample->temperature * 100.0);
            auto pres_pa = static_cast<int32_t>(sample->pressure);
            auto hum_centi = static_cast<int32_t>(sample->humidity * 100.0);

            auto temp_sign = char{};
            auto temp_whole = int32_t{};
            auto temp_frac = int32_t{};
            split_centi(temp_centi, temp_sign, temp_whole, temp_frac);

            auto hum_sign = char{};
            auto hum_whole = int32_t{};
            auto hum_frac = int32_t{};
            split_centi(hum_centi, hum_sign, hum_whole, hum_frac);

            logi("T=%c%d.%02d C  P=%d Pa  H=%c%d.%02d %%",
                 temp_sign, static_cast<int>(temp_whole), static_cast<int>(temp_frac),
                 static_cast<int>(pres_pa),
                 hum_sign, static_cast<int>(hum_whole), static_cast<int>(hum_frac));

            cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                       "BME280 sample: T=%c%d.%02d C  P=%d Pa  H=%c%d.%02d %%\n",
                       temp_sign, static_cast<int>(temp_whole), static_cast<int>(temp_frac),
                       static_cast<int>(pres_pa),
                       hum_sign, static_cast<int>(hum_whole), static_cast<int>(hum_frac));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================================
// sentinel::test::bme280::task_create
// ============================================================================

BaseType_t sentinel::test::bme280::task_create() {
    // configMINIMAL_STACK_SIZE is too lean for the Bosch driver's
    // double-precision compensation paths plus the test's logging frames;
    // the BMP388 reference settled on 4× minimum, which we mirror here.
    constexpr auto stack_words = configMINIMAL_STACK_SIZE * 4;
    constexpr auto priority =
        static_cast<UBaseType_t>(configMAX_PRIORITIES - 3);

    return xTaskCreate([](void *) -> void { sentinel::test::bme280::all(); },
                       "BME280 Test Task", stack_words, nullptr, priority,
                       nullptr);
}
