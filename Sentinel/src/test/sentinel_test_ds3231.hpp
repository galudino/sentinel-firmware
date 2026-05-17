///
/// \file    sentinel_test_ds3231.hpp
/// \brief   DS3231 RTC driver test declarations
///
/// \details This header declares the testbench-side smoke tests for the
///          \ref sentinel::ds3231 driver. Tests exercise the full public
///          API surface of \ref sentinel::ds3231 against a physical DS3231
///          attached to the board's primary I²C bus
///          (\c sentinel::resource::cybsp_i2c).
///
///          Test coverage:
///          - Presence check (status register reachable, OSF state logged)
///          - Aging-offset register round-trip
///            (get / mutate / set / get-back, harmless single byte)
///          - Current time read (no destructive write)
///          - Temperature read with forced conversion + BSY polling
///          - Alarm 1 and Alarm 2 round-trip across multiple match modes
///          - Continuous time + temperature readout at ~1 Hz
///
///          Each test logs progress through both the BLE debug stream
///          (\c logi / \c loge) and the retarget-IO UART serial monitor
///          (\c cy_log_msg). \c logi / \c loge are best-effort writes into
///          a 256-byte ring buffer drained by the BLE debug-stream task;
///          \c cy_log_msg always reaches the UART, so it is used for
///          PASS / FAIL summary lines that must not be dropped.
///
///          The test task is created by \ref task_create and is intended
///          to be called from \c sentinel::testbench::create_tests().
///
/// \author  galudino
/// \date    2026-05-16
/// \version 1.0 - DS3231 test declarations
///

#ifndef SENTINEL_TEST_DS3231_HPP
#define SENTINEL_TEST_DS3231_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

namespace sentinel::test::ds3231 {

///
/// \brief Run the full DS3231 test suite.
///
/// \details Executes each individual test in sequence:
///          \ref presence_check, \ref register_round_trip,
///          \ref time_read, \ref temperature_read,
///          \ref alarm_round_trip, then \ref continuous_read.
///          The continuous-read step runs forever — earlier tests serve
///          as gating preconditions and abort the suite on failure.
///
void all();

///
/// \brief Verify the DS3231 is reachable on the configured bus.
///
/// \details Constructs a \ref sentinel::ds3231 over
///          \c sentinel::resource::cybsp_i2c at address \c 0x68, reads the
///          status register, and verifies the response is not an all-ones
///          (likely indicating a bus pullup with no slave) or all-zeros
///          response with a transport error. Also logs the oscillator-stop
///          flag (OSF) so first-power-up state is visible.
///
void presence_check();

///
/// \brief Verify a single-byte register round-trip via aging-offset.
///
/// \details Uses register \c 0x10 (aging offset) because it is the only
///          freely-writable single-byte register whose contents do not
///          affect timekeeping observably between the write and the
///          read-back. Snapshots the original value, writes a distinct
///          value, reads back to confirm, then restores the original.
///
void register_round_trip();

///
/// \brief Read the current date/time without modifying it.
///
/// \details Performs three independent reads of the current time and
///          logs each. Useful for confirming that the DS3231 is actually
///          counting (successive reads should differ in seconds) and that
///          the BCD-decoded fields look sensible. Does not write to any
///          time registers, so the user's existing time setting is
///          preserved.
///
void time_read();

///
/// \brief Write a new date/time and read it back to confirm.
///
/// \details Writes a known test pattern (\c 2024-02-29 \c 12:34:56 Thursday) to
///          the time registers and reads back every field to verify the round
///          trip. The pattern is chosen to exercise:
///          - leap-year handling (Feb 29 only exists in years divisible by 4
///            and not by 100 unless also by 400, so an off-by-one in
///            \c is_valid() would reject it);
///          - distinct BCD nibbles across all fields so any decode error is
///            unambiguous in the readback log;
///          - a year in the DS3231-addressable range (2000–2199) so
///            \c set_time() accepts it.
///
///          On success, also clears the oscillator-stop flag (OSF) so a
///          subsequent \ref presence_check no longer warns that time may be
///          invalid. \b Does \b not restore the original time — after this
///          test the DS3231 will report the test pattern (plus any seconds
///          that have elapsed since it was written).
///
void time_write();

///
/// \brief Sync the RTC to this firmware's build time (plus a fudge).
///
/// \details Convenience wrapper around
///          \c sentinel::build_time::sync_from_build_time that targets the
///          DS3231 over the shared test transport. Captures \c __DATE__
///          and \c __TIME__ at compile time, applies a small fudge to
///          compensate for flash + boot latency, writes the result to the
///          RTC, then clears the oscillator-stop flag. Useful for getting
///          a freshly-flashed board onto a sensible wall-clock time before
///          the BLE client app is available; rebuild + reflash to resync.
///
///          The captured time reflects the build host's \b local time,
///          not UTC. If the host is not on UTC and you want the RTC to
///          hold UTC, you will need to extend this test (or call
///          \c sync_from_build_time directly) with a timezone offset.
///
void time_sync_from_build();

///
/// \brief Read the on-die temperature, including a forced conversion.
///
/// \details Reads the temperature at startup, forces a one-shot
///          conversion via the CONV bit, polls the BSY flag until the
///          conversion completes, then re-reads. Logs both readings with
///          2-decimal precision in integer-scaled centi-degrees so no
///          \c printf float formatter is invoked.
///
void temperature_read();

///
/// \brief Configure and read back both alarms across multiple match modes.
///
/// \details For each of Alarm 1 and Alarm 2, programmes a representative
///          match-mode configuration, reads it back, and verifies every
///          decoded field matches. Leaves alarm-interrupt enables in the
///          control register \b off so the test does not unexpectedly
///          assert the INT/SQW pin. Clears both alarm flags before
///          returning.
///
void alarm_round_trip();

///
/// \brief Continuously read time + temperature at ~1 Hz.
///
/// \details Runs an infinite loop reading the current time and
///          temperature once per second and logging them in a single
///          formatted line. Both \c logi (BLE debug stream) and
///          \c cy_log_msg (UART) carry the sample so it is visible to
///          either consumer.
///
[[noreturn]] void continuous_read();

///
/// \brief Create the FreeRTOS task that runs \ref all.
///
/// \details Spawns a task at priority \c (configMAX_PRIORITIES - 3) with
///          \c configMINIMAL_STACK_SIZE * 4 words of stack. The task is
///          named \c "DS3231 Test Task" so it shows up clearly in any
///          FreeRTOS task viewer.
///
/// \return \c pdPASS on successful creation, otherwise the
///         \c xTaskCreate failure code.
///
BaseType_t task_create();

} // namespace sentinel::test::ds3231

#endif /* SENTINEL_TEST_DS3231_HPP */
