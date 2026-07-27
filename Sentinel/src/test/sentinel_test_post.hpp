///
/// \file    sentinel_test_post.hpp
/// \brief   Power-On Self-Test (POST) test declarations
///
/// \details Declares the testbench-side tests for the Power-On Self-Test
///          (firmware #35). POST's per-subsystem probes are duck-typed on the
///          drivers they test, so the suite drives them with tiny fake driver
///          doubles -- this exercises every result code deterministically,
///          off-bench, without needing to physically tamper with hardware. The
///          same probe code runs against the real drivers in the application.
///
///          Test coverage maps onto the issue's acceptance criteria. The fakes
///          reproduce the *logic* of each criterion; the physical trigger
///          (disconnect a pin, swap a part, pull the RTC battery) is a manual
///          on-bench step documented per test:
///          - \c all_pass_path        -- every healthy probe passes; summary
///            reports all_passed, failure_count 0; the log gets one
///            post_passed.
///          - \c bme280_disconnect    -- a non-responding BME280 yields
///            fail_no_ack for the bme280 subsystem.
///          - \c w25q128_unknown_jedec -- an unknown JEDEC id yields
///            fail_wrong_id with the manufacturer byte as detail.
///          - \c oscillator_stop      -- a set OSF yields fail_self_test and
///            the probe clears the flag.
///          - \c degraded_operation   -- one forced failure is reported and
///            the summary still enumerates every other subsystem (boot is never
///            halted by POST).
///          - \c records_failures     -- record_results emits exactly one
///            post_subsystem_failed per failure with the right fields.
///          - \c record_store_fallback -- when the record store itself fails,
///            no event-log writes are attempted (debug-stream-only fallback).
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - POST test declarations
///

#ifndef SENTINEL_TEST_POST_HPP
#define SENTINEL_TEST_POST_HPP

#include "sentinel_test_result.hpp"

namespace sentinel::test::post {

///
/// \brief Run the full POST test suite to completion.
///
/// \details Drives every POST probe path against fake driver doubles --
///          all-pass, per-subsystem failures, degraded operation, and the
///          record-store fallback -- and returns the pass/fail
///          \ref sentinel::test::tally. Run-to-completion (#48): no longer
///          self-schedules as a FreeRTOS task; the serial test orchestrator
///          calls \ref sentinel::test::post::run_all directly.
///
/// \return The suite's pass/fail tally.
///
tally run_all() noexcept;

} // namespace sentinel::test::post

#endif /* SENTINEL_TEST_POST_HPP */
