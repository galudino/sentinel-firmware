///
/// \file    sentinel_test_bme280.hpp
/// \brief   BME280 sensor driver test suite (run-to-completion)
///
/// \details Declares the testbench smoke-test suite for the
///          \ref sentinel::bme280 driver. The suite exercises the driver's
///          public API against a physical BME280 attached to the board's
///          primary I²C bus (\c sentinel::resource::cybsp_i2c_bus).
///
///          Test coverage:
///          - Chip ID verification (expected: 0x60 / \c BME280_CHIP_ID)
///          - Soft-reset behavior and post-reset chip-ID re-read
///          - Sensor settings round-trip (get / mutate / set / get-back)
///          - Power-mode transitions
///            (\c SLEEP -> \c FORCED -> \c SLEEP)
///
///          Run-to-completion (#48): the suite runs synchronously and returns
///          a \ref sentinel::test::tally. It no longer self-schedules as a
///          FreeRTOS task and no longer owns a continuous ~1 Hz read loop —
///          the serial test orchestrator calls
///          \ref sentinel::test::bme280::run_all directly, and the
///          continuous BME280 read is owned by
///          \c sentinel::task::bme280_service once the one-shot suite finishes.
///
///          Internally the suite uses a TU-local fixture that owns the
///          bus-arbitrated transport (the canonical "fixture holds the shared
///          resource" shape), so there is no file-static bus global.
///
///          Each test logs progress through the unified logging facade
///          (\c logi / \c loge, #50), which formats each line once and fans it
///          out to both the retarget-IO UART serial monitor and the BLE
///          debug stream. PASS / FAIL summary lines carry the test verdict.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 2.0 - Run-to-completion suite (#48)
///

#ifndef SENTINEL_TEST_BME280_HPP
#define SENTINEL_TEST_BME280_HPP

#include "sentinel_test_result.hpp"

namespace sentinel::test::bme280 {

///
/// \brief Run the full BME280 test suite to completion.
///
/// \details Executes each test in sequence — chip-ID read, soft reset,
///          settings round-trip, power-mode transitions — over a fixture-owned
///          bus transport, and returns the pass/fail \ref sentinel::test::tally.
///          Intended to be called by the testbench serial orchestrator (#48).
///
/// \return The suite's pass/fail tally.
///
tally run_all() noexcept;

} // namespace sentinel::test::bme280

#endif /* SENTINEL_TEST_BME280_HPP */
