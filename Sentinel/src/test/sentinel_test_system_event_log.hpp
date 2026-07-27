///
/// \file    sentinel_test_system_event_log.hpp
/// \brief   System Event Log test declarations
///
/// \details Declares the testbench-side tests for the System Event Log
///          (firmware #34). The log is templated on its record store, so the
///          suite validates it against the RAM-backed
///          \ref sentinel::ram_record_store rather than the flash store — this
///          avoids wearing out the W25Q128 and keeps timestamps deterministic
///          (the tests inject a controllable clock). The same log code runs
///          over the flash \ref sentinel::record_store in the application.
///
///          Test coverage maps 1:1 onto the issue's acceptance criteria:
///          - \c presence_check               — initialize() on a fresh store
///            succeeds and count() == 0
///          - \c record_and_read              — boot sequence yields one
///            readable boot_complete with a non-zero timestamp
///          - \c typed_round_trip             — a firmware_update_record
///            survives memcpy through the untyped store and back
///          - \c record_burst                 — 1000 mixed events all persist
///          - \c survive_reset                — records + ordering survive a
///            simulated warm reboot; boot adds exactly one record
///          - \c unexpected_shutdown_synthesis — an unclean reboot synthesizes
///            a shutdown_unexpected for the prior session
///          - \c erase_all                    — erase resets count to 0 and
///            new records still append
///          - \c crossing_size_threshold      — wrap overwrites the oldest,
///            keeps the newest readable
///
///          Each test drives the drain path synchronously via
///          \c drain_pending() so results are deterministic; the production
///          FreeRTOS drain task calls the very same code.
///
///          Every PASS / FAIL line goes through \c logi / \c loge, which
///          the logging facade (#50) writes to both the retarget-IO UART and
///          the BLE debug stream.
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - System Event Log test declarations
///

#ifndef SENTINEL_TEST_SYSTEM_EVENT_LOG_HPP
#define SENTINEL_TEST_SYSTEM_EVENT_LOG_HPP

#include "sentinel_test_result.hpp"

namespace sentinel::test::system_event_log {

///
/// \brief Run the full System Event Log test suite to completion.
///
/// \details Validates the log over the RAM-backed
///          \ref sentinel::ram_record_store across all eight acceptance
///          criteria — presence, record/read, typed round-trip, burst, warm
///          reboot, unexpected-shutdown synthesis, erase, and wrap — and
///          returns the pass/fail \ref sentinel::test::tally. Run-to-completion
///          (#48): no longer self-schedules as a FreeRTOS task; the serial test
///          orchestrator calls \ref sentinel::test::system_event_log::run_all
///          directly.
///
/// \return The suite's pass/fail tally.
///
tally run_all() noexcept;

} // namespace sentinel::test::system_event_log

#endif /* SENTINEL_TEST_SYSTEM_EVENT_LOG_HPP */
