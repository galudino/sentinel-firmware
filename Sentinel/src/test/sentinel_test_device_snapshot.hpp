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
///          and are manual on-bench steps — \ref populate_snapshot reads the
///          #37 / rtc_service caches, which only carry real values on hardware.
///
/// \author  galudino
/// \date    2026-06-29
/// \version 1.0 - device_snapshot test declarations
///

#ifndef SENTINEL_TEST_DEVICE_SNAPSHOT_HPP
#define SENTINEL_TEST_DEVICE_SNAPSHOT_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

namespace sentinel::test::device_snapshot {

/// \brief Run the full device_snapshot test suite.
void all();

/// \brief sizeof(device_snapshot) is exactly the 80-byte wire size.
void size_invariant();

/// \brief Known field values land at their documented little-endian byte offsets.
void byte_layout();

/// \brief Serialize to a byte buffer and back; every field survives unchanged.
void round_trip();

/// \brief An unknown future snapshot_version still exposes readable header fields.
void forward_compat_probe();

/// \brief A zeroed/torn record is distinguishable from a valid one by the magic.
void trailer_magic();

/// \brief Create the FreeRTOS task that runs \ref all.
BaseType_t task_create();

} // namespace sentinel::test::device_snapshot

#endif /* SENTINEL_TEST_DEVICE_SNAPSHOT_HPP */
