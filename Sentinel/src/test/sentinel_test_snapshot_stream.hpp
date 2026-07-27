///
/// \file    sentinel_test_snapshot_stream.hpp
/// \brief   Live snapshot stream task test declarations (#46)
///
/// \details Declares the testbench-side suite for the live snapshot stream task
///          (firmware #46). Unlike the pure device_snapshot wire-layout tests
///          (#36), these are \b behavioral: they drive the real
///          \ref sentinel::task::snapshot_stream_task singleton through its
///          idle ↔ stream lifecycle and observe a counting notify sink, so the
///          steps are ordered and stateful (they share one running task and run
///          in sequence with cadence delays) rather than independent pure
///          bodies.
///
///          The task is created in the testbench's \c create_tasks(); this
///          driver attaches a counting sink and a controllable connection
///          predicate to that singleton and exercises:
///            - \c idle_by_default      — created-but-not-started emits
///            nothing;
///            - \c start_stop           — start/stop toggle streaming,
///            idempotent;
///            - \c cadence              — sink fires at ~period_ms while
///            streaming;
///            - \c disconnect_autostop  — a simulated disconnect returns to
///            idle;
///            - \c populate_is_cache_backed — streamed snapshots are complete
///            with
///              no bus arbiter driving them (populate reads caches only).
///
/// \author  galudino
/// \date    2026-06-29
/// \version 1.0 - snapshot stream task test declarations
///

#ifndef SENTINEL_TEST_SNAPSHOT_STREAM_HPP
#define SENTINEL_TEST_SNAPSHOT_STREAM_HPP

#include "sentinel_test_result.hpp"

namespace sentinel::test::snapshot_stream {

///
/// \brief Run the full snapshot stream task behavioral suite to completion.
///
/// \details Ordered, stateful behavioral suite over the real
///          \ref sentinel::task::snapshot_stream_task singleton (idle ↔ stream
///          lifecycle, cadence, disconnect auto-stop). Run-to-completion (#48):
///          no longer self-schedules as a FreeRTOS task; the serial test
///          orchestrator calls \ref sentinel::test::snapshot_stream::run_all
///          directly, after the singleton has been created. Returns the
///          pass/fail \ref sentinel::test::tally.
///
/// \return The suite's pass/fail tally.
///
tally run_all() noexcept;

} // namespace sentinel::test::snapshot_stream

#endif /* SENTINEL_TEST_SNAPSHOT_STREAM_HPP */
