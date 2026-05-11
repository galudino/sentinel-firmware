///
/// \file    sentinel_test_bme280.hpp
/// \brief   BME280 sensor driver test declarations
///
/// \details This header declares the testbench-side smoke tests for the
///          \ref sentinel::bme280 driver. Tests exercise the full public
///          API surface of \ref sentinel::bme280 against a physical BME280
///          attached to the board's primary I²C bus
///          (\c sentinel::resource::cybsp_i2c).
///
///          Test coverage:
///          - Chip ID verification (expected: 0x60 / \c BME280_CHIP_ID)
///          - Soft-reset behavior and post-reset chip-ID re-read
///          - Sensor settings round-trip (get / mutate / set / get-back)
///          - Power-mode transitions
///            (\c SLEEP \c \xe2\x86\x92 \c FORCED \c \xe2\x86\x92 \c SLEEP)
///          - Continuous temperature/pressure/humidity reads at ~1 Hz
///
///          Each test logs progress through both the BLE debug stream
///          (\c logi / \c loge) and the retarget-IO UART serial monitor
///          (\c cy_log_msg). \c logi / \c loge are best-effort writes into
///          a 256-byte ring buffer drained by the BLE debug-stream task;
///          \c cy_log_msg always reaches the UART, so it is used for
///          PASS / FAIL summary lines that must not be dropped.
///
///          The test task is created by \ref task_create and is intended to
///          be called from \c sentinel::testbench::create_tests().
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0 - BME280 test declarations
///

#ifndef SENTINEL_TEST_BME280_HPP
#define SENTINEL_TEST_BME280_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

namespace sentinel::test::bme280 {

///
/// \brief Run the full BME280 test suite
///
/// \details Executes each individual test in sequence, in the order:
///          \ref chip_id_read, \ref soft_reset, \ref settings_round_trip,
///          \ref sensor_mode_transitions, \ref continuous_read.
///          The continuous-read step runs forever — earlier tests serve as
///          gating preconditions and abort the suite on failure.
///
void all();

///
/// \brief Verify BME280 presence over the configured bus
///
/// \details Constructs a \ref sentinel::bme280 over
///          \c sentinel::resource::cybsp_i2c, reads register \c 0xD0, and
///          checks the response against \c BME280_CHIP_ID (0x60). Logs
///          PASS / FAIL through both \c logi / \c loge (BLE debug stream)
///          and \c cy_log_msg (UART).
///
void chip_id_read();

///
/// \brief Verify soft-reset behavior
///
/// \details Issues \c bme280_soft_reset, waits for the part to be
///          accessible again, and re-reads the chip ID to confirm the bus
///          and device survived the reset.
///
void soft_reset();

///
/// \brief Verify settings get / mutate / set / get round-trip
///
/// \details Reads the current \c bme280_settings, mutates a known field
///          (filter coefficient), writes the mutated settings back, and
///          reads them again to confirm the new value is reflected on the
///          device. Restores the original value before returning.
///
void settings_round_trip();

///
/// \brief Verify power-mode transitions
///
/// \details Cycles the device through
///          \c BME280_POWERMODE_SLEEP, \c BME280_POWERMODE_FORCED, and back
///          to \c BME280_POWERMODE_SLEEP, calling \c get_sensor_mode after
///          each transition. \c FORCED is self-clearing so the assertion
///          after the forced step tolerates either \c FORCED or \c SLEEP.
///
void sensor_mode_transitions();

///
/// \brief Continuously read T/P/H at ~1 Hz
///
/// \details Runs an infinite loop performing a forced-mode
///          \c read_temperature_pressure_humidity every second and
///          formatting the result as integer-scaled millidegrees / pascals
///          / millipercent so that no \c printf float formatter is invoked
///          (per the project-wide constraint described in
///          \c sentinel_debug_print.hpp).
///
[[noreturn]] void continuous_read();

///
/// \brief Create the FreeRTOS task that runs \ref all
///
/// \details Spawns a task at priority \c (configMAX_PRIORITIES - 3) with
///          stack large enough to hold the temporary buffers used by
///          \c bme280_set_regs / \c bme280_get_regs (the Bosch driver
///          allocates a small on-stack scratch buffer internally) plus the
///          test's own logging frames. The task is named
///          \c "BME280 Test Task" so it shows up clearly in any FreeRTOS
///          task viewer.
///
/// \return \c pdPASS on successful creation, otherwise the
///         \c xTaskCreate failure code.
///
BaseType_t task_create();

} // namespace sentinel::test::bme280

#endif /* SENTINEL_TEST_BME280_HPP */
