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
///          - \ref presence_check               — initialize() on a fresh store
///            succeeds and count() == 0
///          - \ref record_and_read              — boot sequence yields one
///            readable boot_complete with a non-zero timestamp
///          - \ref typed_round_trip             — a firmware_update_record
///            survives memcpy through the untyped store and back
///          - \ref record_burst                 — 1000 mixed events all persist
///          - \ref survive_reset                — records + ordering survive a
///            simulated warm reboot; boot adds exactly one record
///          - \ref unexpected_shutdown_synthesis— an unclean reboot synthesizes
///            a shutdown_unexpected for the prior session
///          - \ref erase_all                    — erase resets count to 0 and
///            new records still append
///          - \ref crossing_size_threshold      — wrap overwrites the oldest,
///            keeps the newest readable
///
///          Each test drives the drain path synchronously via
///          \c drain_pending() so results are deterministic; the production
///          FreeRTOS drain task calls the very same code.
///
///          Per project convention every PASS / FAIL line goes to both the BLE
///          debug stream (\c logi / \c loge) and the retarget-IO UART
///          (\c cy_log_msg).
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - System Event Log test declarations
///

#ifndef SENTINEL_TEST_SYSTEM_EVENT_LOG_HPP
#define SENTINEL_TEST_SYSTEM_EVENT_LOG_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

namespace sentinel::test::system_event_log {

/// \brief Run the full System Event Log test suite.
void all();

/// \brief A fresh log over a freshly-erased store reports empty.
void presence_check();

/// \brief The boot sequence records one readable boot_complete.
void record_and_read();

/// \brief A typed firmware_update_record round-trips through the store.
void typed_round_trip();

/// \brief 1000 mixed events are all persisted without loss.
void record_burst();

/// \brief Records and ordering survive a simulated warm reboot.
void survive_reset();

/// \brief An unclean reboot synthesizes a shutdown_unexpected.
void unexpected_shutdown_synthesis();

/// \brief erase_all() empties the log and appends still work afterwards.
void erase_all();

/// \brief Filling past capacity wraps: oldest overwritten, newest readable.
void crossing_size_threshold();

/// \brief Create the FreeRTOS task that runs \ref all.
BaseType_t task_create();

} // namespace sentinel::test::system_event_log

#endif /* SENTINEL_TEST_SYSTEM_EVENT_LOG_HPP */
