///
/// \file    sentinel_test_device_snapshot.hpp
/// \brief   device_snapshot test declarations (#36)
///
/// \details Declares the testbench-side tests for the device_snapshot wire
///          contract (firmware #36). These tests are pure and off-bench: they
///          exercise the byte layout, round-trip serialization, and forward-
///          compatibility of the packed struct with no hardware.
///
///          The struct's exact size and every field offset are additionally
///          locked at compile time by \c static_assert in
///          \c sentinel_device_snapshot.hpp; these runtime tests confirm the
///          same invariants observably and cover serialization behavior the
///          asserts cannot. The two populate acceptance criteria
///          (populate_default / populate_partial) need a live BME280 + DS3231
///          and are manual on-bench steps — \c populate_snapshot() reads the
///          #37 / rtc_service caches, which only carry real values on hardware.
///
/// \author  galudino
/// \date    2026-06-29
/// \version 1.0 - device_snapshot test declarations
///

#ifndef SENTINEL_TEST_DEVICE_SNAPSHOT_HPP
#define SENTINEL_TEST_DEVICE_SNAPSHOT_HPP

#include "sentinel_test_result.hpp"

namespace sentinel::test::device_snapshot {

///
/// \brief Run the full device_snapshot test suite to completion.
///
/// \details Pure, off-bench checks of the packed wire contract — size
///          invariant, byte layout, round-trip serialization, forward-compat
///          probe, trailer magic — returning the pass/fail
///          \ref sentinel::test::tally. Run-to-completion (#48): no longer
///          self-schedules as a FreeRTOS task; the serial test orchestrator
///          calls \ref sentinel::test::device_snapshot::run_all directly.
///
/// \return The suite's pass/fail tally.
///
tally run_all() noexcept;

} // namespace sentinel::test::device_snapshot

#endif /* SENTINEL_TEST_DEVICE_SNAPSHOT_HPP */
