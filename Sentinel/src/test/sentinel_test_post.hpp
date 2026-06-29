///
/// \file    sentinel_test_post.hpp
/// \brief   Power-On Self-Test (POST) test declarations
///
/// \details Declares the testbench-side tests for the Power-On Self-Test
///          (firmware #35). POST's per-subsystem probes are duck-typed on the
///          drivers they test, so the suite drives them with tiny fake driver
///          doubles — this exercises every result code deterministically,
///          off-bench, without needing to physically tamper with hardware. The
///          same probe code runs against the real drivers in the application.
///
///          Test coverage maps onto the issue's acceptance criteria. The fakes
///          reproduce the *logic* of each criterion; the physical trigger
///          (disconnect a pin, swap a part, pull the RTC battery) is a manual
///          on-bench step documented per test:
///          - \ref all_pass_path        — every healthy probe passes; summary
///            reports all_passed, failure_count 0; the log gets one post_passed.
///          - \ref bme280_disconnect    — a non-responding BME280 yields
///            fail_no_ack for the bme280 subsystem.
///          - \ref w25q128_unknown_jedec— an unknown JEDEC id yields
///            fail_wrong_id with the manufacturer byte as detail.
///          - \ref oscillator_stop      — a set OSF yields fail_self_test and
///            the probe clears the flag.
///          - \ref degraded_operation   — one forced failure is reported and
///            the summary still enumerates every other subsystem (boot is never
///            halted by POST).
///          - \ref records_failures     — record_results emits exactly one
///            post_subsystem_failed per failure with the right fields.
///          - \ref record_store_fallback— when the record store itself fails,
///            no event-log writes are attempted (debug-stream-only fallback).
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - POST test declarations
///

#ifndef SENTINEL_TEST_POST_HPP
#define SENTINEL_TEST_POST_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

namespace sentinel::test::post {

/// \brief Run the full POST test suite.
void all();

/// \brief All healthy probes pass; summary clean; one post_passed recorded.
void all_pass_path();

/// \brief A non-responding BME280 yields fail_no_ack.
void bme280_disconnect();

/// \brief An unknown flash JEDEC id yields fail_wrong_id + manufacturer detail.
void w25q128_unknown_jedec();

/// \brief A set oscillator-stop flag yields fail_self_test and is cleared.
void oscillator_stop();

/// \brief One forced failure is reported; every subsystem is still enumerated.
void degraded_operation();

/// \brief record_results emits one post_subsystem_failed per failure.
void records_failures();

/// \brief A failed record store suppresses event-log writes (debug-only).
void record_store_fallback();

/// \brief Create the FreeRTOS task that runs \ref all.
BaseType_t task_create();

} // namespace sentinel::test::post

#endif /* SENTINEL_TEST_POST_HPP */
