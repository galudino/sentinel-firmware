///
/// \file    sentinel_test_ds3231.hpp
/// \brief   DS3231 RTC driver test suite (run-to-completion)
///
/// \details Declares the testbench smoke-test suite for the
///          \ref sentinel::ds3231 driver. The suite exercises the driver's
///          public API against a physical DS3231 attached to the board's
///          primary I²C bus (\c sentinel::resource::cybsp_i2c_bus).
///
///          Test coverage:
///          - Presence check (status register reachable, OSF state logged)
///          - Aging-offset register round-trip
///            (get / mutate / set / get-back, harmless single byte)
///          - Current time read (no destructive write)
///          - Time write + readback (leap-year test pattern)
///          - Build-time sync convenience
///          - Temperature read with forced conversion + BSY polling
///          - Alarm 1 and Alarm 2 round-trip across multiple match modes
///
///          Run-to-completion (#48): the suite runs synchronously and returns
///          a \ref sentinel::test::tally. It no longer self-schedules as a
///          FreeRTOS task and no longer owns a continuous ~1 Hz read loop —
///          the serial test orchestrator calls \ref run_all directly, and the
///          continuous time/temperature read is owned by
///          \c sentinel::task::rtc_service once the one-shot suite finishes.
///
///          Internally the suite uses a TU-local fixture that owns the
///          bus-arbitrated transport (the canonical "fixture holds the shared
///          resource" shape), so there is no file-static bus global.
///
///          Each test logs progress via \c logi / \c loge; the logging
///          facade (#50) writes each line to both the retarget-IO UART serial
///          monitor and the BLE debug stream.
///
/// \author  galudino
/// \date    2026-05-16
/// \version 2.0 - Run-to-completion suite (#48)
///

#ifndef SENTINEL_TEST_DS3231_HPP
#define SENTINEL_TEST_DS3231_HPP

#include "sentinel_test_result.hpp"

namespace sentinel::test::ds3231 {

///
/// \brief Run the full DS3231 test suite to completion.
///
/// \details Executes each test in sequence — presence check, register
///          round-trip, time read, time write, build-time sync, temperature
///          read, alarm round-trip — over a fixture-owned bus transport, and
///          returns the pass/fail \ref sentinel::test::tally. Intended to be
///          called by the testbench serial orchestrator (#48).
///
/// \return The suite's pass/fail tally.
///
tally run_all() noexcept;

} // namespace sentinel::test::ds3231

#endif /* SENTINEL_TEST_DS3231_HPP */
