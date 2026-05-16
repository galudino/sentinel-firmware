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
/// \version 1.0 - BME280 test implementation
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
#include "sentinel_cyhal_i2c_transport.hpp"
#include "sentinel_debug_print.hpp"
#include "sentinel_resource.hpp"
#include "sentinel_test_bme280.hpp"
#include "sentinel_utilities.hpp"

#include <cstdint>

namespace {

///
/// \brief Bus instance used by every BME280 test
///
/// \details Constructed once at translation-unit scope so each individual
///          test can simply do \c sentinel::bme280<...> \c sensor(bme280_bus);
///          without re-wiring CYHAL handles. Storage lives in BSS until
///          \c peripheral_initialize() populates
///          \c sentinel::resource::cybsp_i2c, so the transport itself is
///          inert until the testbench has finished hardware init.
///
sentinel::cyhal_i2c_transport bme280_bus(&sentinel::resource::cybsp_i2c,
                                         BME280_I2C_ADDR_PRIM);

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
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("BME280 chip_id_read: sensor init OK", "");
    yield_for_debug_drain(200);

    auto chip_id = uint8_t{0xFF};
    auto result = sensor.read_chip_id(chip_id);

    if (result != BME280_OK) {
        loge("chip_id_read FAIL: I2C error %d", (int)result);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 chip_id_read FAIL: I2C error %d\n", (int)result);
        return;
    }

    if (chip_id == BME280_CHIP_ID) {
        logi("chip_id_read PASS: 0x%02X (%d)", (int)chip_id, (int)chip_id);
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "BME280 chip_id_read PASS: 0x%02X\n", (int)chip_id);
        return;
    }

    loge("chip_id_read FAIL: got 0x%02X, expected 0x%02X", (int)chip_id,
         (int)BME280_CHIP_ID);
    cy_log_msg(CYLF_DEF, CY_LOG_ERR,
               "BME280 chip_id_read FAIL: got 0x%02X, expected 0x%02X\n",
               (int)chip_id, (int)BME280_CHIP_ID);
}

// ============================================================================
// sentinel::test::bme280::soft_reset
// ============================================================================

void sentinel::test::bme280::soft_reset() {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("soft_reset: sensor init OK", "");
    yield_for_debug_drain(200);

    auto result = sensor.soft_reset();

    if (result != BME280_OK) {
        loge("soft_reset FAIL: reset call returned %d", (int)result);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 soft_reset FAIL: %d\n", (int)result);
        return;
    }

    // Datasheet specifies a 2 ms startup delay after reset before the part
    // is ready to accept further commands.
    yield_for_debug_drain(5);

    // Confirm the bus is still good and the part still answers with the
    // correct chip ID after the reset.
    auto chip_id = uint8_t{0xFF};
    auto id_result = sensor.read_chip_id(chip_id);

    if (id_result != BME280_OK || chip_id != BME280_CHIP_ID) {
        loge("soft_reset FAIL: post-reset chip ID 0x%02X (rc=%d)",
             (int)chip_id, (int)id_result);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 soft_reset FAIL: post-reset chip ID 0x%02X\n",
                   (int)chip_id);
        return;
    }

    logi("soft_reset PASS: chip ID 0x%02X after reset", (int)chip_id);
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "BME280 soft_reset PASS\n");
}

// ============================================================================
// sentinel::test::bme280::settings_round_trip
// ============================================================================

void sentinel::test::bme280::settings_round_trip() {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("settings_round_trip: sensor init OK", "");
    yield_for_debug_drain(200);

    // Snapshot the device's current settings so we can restore them.
    auto original = bme280_settings{};
    auto result = sensor.get_sensor_settings(original);

    if (result != BME280_OK) {
        loge("settings_round_trip FAIL: initial get returned %d", (int)result);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 settings_round_trip FAIL: initial get %d\n",
                   (int)result);
        return;
    }

    logi("settings_round_trip: original filter=%d osr_t=%d osr_p=%d osr_h=%d",
         (int)original.filter, (int)original.osr_t, (int)original.osr_p,
         (int)original.osr_h);
    yield_for_debug_drain(200);

    // Mutate the filter coefficient to a value distinct from the driver's
    // default (which is FILTER_COEFF_16). The check below tolerates either
    // outcome — we just need the device to round-trip *some* observable
    // change.
    auto mutated = original;
    auto target_filter = (original.filter == BME280_FILTER_COEFF_8)
                             ? BME280_FILTER_COEFF_4
                             : BME280_FILTER_COEFF_8;
    mutated.filter = target_filter;

    result = sensor.set_sensor_settings(BME280_SEL_FILTER, mutated);
    if (result != BME280_OK) {
        loge("settings_round_trip FAIL: set returned %d", (int)result);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 settings_round_trip FAIL: set %d\n", (int)result);
        return;
    }

    // Read back and confirm.
    auto readback = bme280_settings{};
    result = sensor.get_sensor_settings(readback);
    if (result != BME280_OK) {
        loge("settings_round_trip FAIL: readback get returned %d", (int)result);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 settings_round_trip FAIL: readback %d\n",
                   (int)result);
        return;
    }

    if (readback.filter != target_filter) {
        loge("settings_round_trip FAIL: filter %d != expected %d",
             (int)readback.filter, (int)target_filter);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 settings_round_trip FAIL: filter %d != %d\n",
                   (int)readback.filter, (int)target_filter);
    } else {
        logi("settings_round_trip PASS: filter %d", (int)readback.filter);
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "BME280 settings_round_trip PASS: filter %d\n",
                   (int)readback.filter);
    }

    // Restore original settings — best-effort; a failure here is logged but
    // does not affect the test result.
    auto restore_result =
        sensor.set_sensor_settings(BME280_SEL_FILTER, original);
    if (restore_result != BME280_OK) {
        logw("settings_round_trip: restore returned %d", (int)restore_result);
    }
}

// ============================================================================
// sentinel::test::bme280::sensor_mode_transitions
// ============================================================================

void sentinel::test::bme280::sensor_mode_transitions() {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("sensor_mode_transitions: sensor init OK", "");
    yield_for_debug_drain(200);

    // Park the part in sleep first; this gives us a known starting state
    // regardless of what configure_sensor() left programmed in the ctrl
    // registers.
    auto result = sensor.set_sensor_mode(BME280_POWERMODE_SLEEP);
    if (result != BME280_OK) {
        loge("sensor_mode_transitions FAIL: set SLEEP returned %d",
             (int)result);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 sensor_mode_transitions FAIL: set SLEEP %d\n",
                   (int)result);
        return;
    }

    auto mode = uint8_t{0xFF};
    result = sensor.get_sensor_mode(mode);
    if (result != BME280_OK || mode != BME280_POWERMODE_SLEEP) {
        loge("sensor_mode_transitions FAIL: expected SLEEP, got %d (rc=%d)",
             (int)mode, (int)result);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 sensor_mode_transitions FAIL: SLEEP readback %d\n",
                   (int)mode);
        return;
    }
    logi("sensor_mode_transitions: SLEEP confirmed (%d)", (int)mode);
    yield_for_debug_drain(100);

    // FORCED is self-clearing: the BME280 returns to SLEEP automatically
    // once the one-shot measurement completes. We sample the mode register
    // immediately after the transition to maximize the chance of catching
    // it before the auto-return.
    result = sensor.set_sensor_mode(BME280_POWERMODE_FORCED);
    if (result != BME280_OK) {
        loge("sensor_mode_transitions FAIL: set FORCED returned %d",
             (int)result);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 sensor_mode_transitions FAIL: set FORCED %d\n",
                   (int)result);
        return;
    }

    result = sensor.get_sensor_mode(mode);
    if (result != BME280_OK) {
        loge("sensor_mode_transitions FAIL: FORCED readback rc=%d",
             (int)result);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 sensor_mode_transitions FAIL: FORCED readback %d\n",
                   (int)result);
        return;
    }

    // Accept either FORCED or SLEEP — the conversion may have already
    // completed by the time we read back.
    if (mode != BME280_POWERMODE_FORCED && mode != BME280_POWERMODE_SLEEP) {
        loge("sensor_mode_transitions FAIL: unexpected mode %d after FORCED",
             (int)mode);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 sensor_mode_transitions FAIL: post-FORCED mode %d\n",
                   (int)mode);
        return;
    }

    logi("sensor_mode_transitions PASS: post-FORCED mode %d", (int)mode);
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "BME280 sensor_mode_transitions PASS: post-FORCED mode %d\n",
               (int)mode);
}

// ============================================================================
// sentinel::test::bme280::continuous_read
// ============================================================================

[[noreturn]] void sentinel::test::bme280::continuous_read() {
    auto sensor = sentinel::bme280_i2c<sentinel::cyhal_i2c_transport>(
        bme280_bus, BME280_I2C_ADDR_PRIM);
    logi("continuous_read: sensor init OK", "");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "BME280 continuous_read: entering 1 Hz loop\n");
    yield_for_debug_drain(200);

    // Gate-check: never enter the loop if the chip ID is wrong.
    auto chip_id = uint8_t{0xFF};
    auto id_result = sensor.read_chip_id(chip_id);

    if (id_result != BME280_OK || chip_id != BME280_CHIP_ID) {
        loge("continuous_read ABORT: chip ID 0x%02X (rc=%d)", (int)chip_id,
             (int)id_result);
        cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                   "BME280 continuous_read ABORT: chip ID 0x%02X\n",
                   (int)chip_id);
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    logi("continuous_read: chip ID OK (0x%02X)", (int)chip_id);
    yield_for_debug_drain(200);

    while (true) {
        auto data = bme280_data{};
        auto result = sensor.read_temperature_pressure_humidity(data);

        if (result != BME280_OK) {
            loge("continuous_read: read error %d", (int)result);
            cy_log_msg(CYLF_DEF, CY_LOG_ERR,
                       "BME280 continuous_read: read error %d\n", (int)result);
        } else {
            // Bosch driver is compiled in BME280_DOUBLE_ENABLE mode by
            // default — pressure/temperature/humidity are doubles in Pa,
            // °C, and %RH respectively. Scale to integers so we never
            // invoke a %f formatter.
            auto temp_centi = static_cast<int32_t>(data.temperature * 100.0);
            auto pres_pa = static_cast<int32_t>(data.pressure);
            auto hum_centi = static_cast<int32_t>(data.humidity * 100.0);

            auto temp_sign = char{};
            auto temp_whole = int32_t{};
            auto temp_frac = int32_t{};
            split_centi(temp_centi, temp_sign, temp_whole, temp_frac);

            auto hum_sign = char{};
            auto hum_whole = int32_t{};
            auto hum_frac = int32_t{};
            split_centi(hum_centi, hum_sign, hum_whole, hum_frac);

            logi("T=%c%d.%02d C  P=%d Pa  H=%c%d.%02d %%",
                 temp_sign, (int)temp_whole, (int)temp_frac,
                 (int)pres_pa,
                 hum_sign, (int)hum_whole, (int)hum_frac);

            cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                       "BME280 sample: T=%c%d.%02d C  P=%d Pa  H=%c%d.%02d %%\n",
                       temp_sign, (int)temp_whole, (int)temp_frac,
                       (int)pres_pa,
                       hum_sign, (int)hum_whole, (int)hum_frac);
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

    return xTaskCreate(
        [](void *) -> void { sentinel::test::bme280::all(); },
        "BME280 Test Task", stack_words, nullptr, priority, nullptr);
}
