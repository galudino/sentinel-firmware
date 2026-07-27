///
/// \file    sentinel_test_snapshot_persistence.hpp
/// \brief   Snapshot persistence task test declarations (lane 1, #38)
///
/// \details Declares the testbench-side suite for the snapshot persistence task
///          (firmware #38, lane 1). Per decision #15 the suite drives the \b
///          real
///          \ref sentinel::task::snapshot_persistence_task singleton — not a
///          throwaway harness — over a small bound scratch flash store, so the
///          capture → append → read-back → wrap logic is validated against the
///          actual record store on the physical W25Q128. The continuous-cadence
///          timing, the boot anchor, and the event-log heartbeat are on-bench
///          observations exercised by the running production task
///          (orchestrator).
///
///          Run-to-completion (#48): the serial test orchestrator calls
///          \ref sentinel::test::snapshot_persistence::run_all directly; it
///          returns the pass/fail \ref sentinel::test::tally.
///
/// \author  galudino
/// \date    2026-06-30
/// \version 1.0 - snapshot persistence task test declarations
///

#ifndef SENTINEL_TEST_SNAPSHOT_PERSISTENCE_HPP
#define SENTINEL_TEST_SNAPSHOT_PERSISTENCE_HPP

#include "sentinel_test_result.hpp"

namespace sentinel::test::snapshot_persistence {

///
/// \brief Run the full snapshot persistence suite to completion.
///
/// \details Binds a scratch store to the real persistence task, then exercises
///          erase / capture / count / read / read_range / wrap-around. Restores
///          the default (shared-context) store before returning.
///
/// \return The suite's pass/fail tally.
///
tally run_all() noexcept;

} // namespace sentinel::test::snapshot_persistence

#endif /* SENTINEL_TEST_SNAPSHOT_PERSISTENCE_HPP */
