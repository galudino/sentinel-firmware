///
/// \file    sentinel_testbench_orchestrator.cpp
/// \brief   One-shot serial test orchestrator task implementation (#48)
///
/// \details Implements \ref sentinel::testbench::test_orchestrator. The task
///          runs the eight test suites bottom-up and serially via each suite's
///          synchronous \c run_all(), prints a per-group pass/fail tally, then
///          starts the continuous reader services and self-deletes. See the
///          header for the full rationale (decision #13, issue #48).
///
/// \author  galudino
/// \date    2026-06-30
/// \version 1.0 - Initial serial test orchestrator
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_log.h"
#include <FreeRTOS.h>
#include <task.h>

///< BSP — provides the CYBSP_I2C_HW / CYBSP_SPI_HW presence macros.
#include "cybsp.h"
}
#pragma GCC diagnostic pop

#include "sentinel_testbench_orchestrator.hpp"

///< Logging
#include "sentinel_debug_print.hpp"

///< Continuous reader / helper services started by the orchestrator
#include "sentinel_task_battery_service.hpp"
#include "sentinel_task_bme280_service.hpp"
#include "sentinel_task_rtc_service.hpp"
#include "sentinel_task_snapshot_stream.hpp"

///< Test suites (each exposes a synchronous run_all() -> tally)
#include "sentinel_test_bme280.hpp"
#include "sentinel_test_device_snapshot.hpp"
#include "sentinel_test_ds3231.hpp"
#include "sentinel_test_post.hpp"
#include "sentinel_test_record_store.hpp"
#include "sentinel_test_result.hpp"
#include "sentinel_test_snapshot_stream.hpp"
#include "sentinel_test_system_event_log.hpp"
#include "sentinel_test_w25q128.hpp"

namespace {

/// Upper bound on test groups (4 driver/storage + event-log + snapshot +
/// POST + snapshot-stream = 8); sized to the maximum so the summary array is
/// fixed-size regardless of which bus-gated groups are compiled in.
constexpr int kMaxGroups = 8;

/// One group's name + result, retained for the final per-group summary.
struct group_result {
    const char *name{nullptr};
    sentinel::test::tally tally{};
};

/// Print a horizontal rule to the UART diagnostic log.
void rule() noexcept {
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "========================================================\n");
}

/// Print a boxed banner to the UART diagnostic log.
void banner(const char *title) noexcept {
    rule();
    cy_log_msg(CYLF_DEF, CY_LOG_INFO, "  %s\n", title);
    rule();
}

} // namespace

namespace sentinel::testbench {

// ============================================================================
// test_orchestrator::instance
// ============================================================================

test_orchestrator &test_orchestrator::instance() noexcept {
    static test_orchestrator the_instance;
    return the_instance;
}

// ============================================================================
// test_orchestrator::task_create
// ============================================================================

BaseType_t test_orchestrator::task_create(bool ble_stack_ok, bool gatt_db_ok,
                                          UBaseType_t priority,
                                          uint16_t stack_words) noexcept {
    m_ble_stack_ok = ble_stack_ok;
    m_gatt_db_ok = gatt_db_ok;
    return xTaskCreate(&test_orchestrator::task_trampoline, "Test Orchestrator",
                       stack_words, this, priority, &m_handle);
}

// ============================================================================
// test_orchestrator::task_trampoline
// ============================================================================

void test_orchestrator::task_trampoline(void *task_parameter) {
    static_cast<test_orchestrator *>(task_parameter)->run();
}

// ============================================================================
// test_orchestrator::run
// ============================================================================

void test_orchestrator::run() {
    banner("SENTINEL TESTBENCH - bottom-up serial diagnostic");

    // Idle helper tasks the orchestrator owns (everything beyond the bus
    // arbiters + debug stream, per decision #13). Both are silent until
    // triggered, so creating them up front is harmless:
    //   - snapshot_stream_task: the snapshot_stream suite drives this
    //   singleton,
    //     so it must exist before that group runs; idle until #6 calls start().
    //   - battery_service: only acts when BLE-connected + notifications
    //   enabled.
    if (task::snapshot_stream_task::instance().task_create() != pdPASS) {
        loge("orchestrator: snapshot_stream_task create failed", "");
    }
    if (task::battery_service::instance().task_create() != pdPASS) {
        loge("orchestrator: battery_service create failed", "");
    }

    group_result groups[kMaxGroups]{};
    auto count = int{0};

    // Run one group to completion: print its header, call the suite's
    // synchronous run_all(), print its result line, and retain the tally.
    auto run_group = [&](const char *name, sentinel::test::tally (*run_all)()) {
        cy_log_msg(CYLF_DEF, CY_LOG_INFO, "\n---- [ %s ] ----\n", name);
        logi("---- [ %s ] ----", name);

        const auto result = run_all();

        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "---- [ %s ] done: %u passed, %u failed ----\n", name,
                   static_cast<unsigned>(result.passed),
                   static_cast<unsigned>(result.failed));

        if (count < kMaxGroups) {
            groups[count].name = name;
            groups[count].tally = result;
            ++count;
        }
    };

    // -------- Bottom-up, dependency-ordered test sequence --------
#ifdef CYBSP_I2C_HW
    run_group("BME280", &sentinel::test::bme280::run_all);
    run_group("DS3231", &sentinel::test::ds3231::run_all);
#endif /* CYBSP_I2C_HW */

#ifdef CYBSP_SPI_HW
    run_group("W25Q128", &sentinel::test::w25q128::run_all);
    run_group("record_store", &sentinel::test::record_store::run_all);
#endif /* CYBSP_SPI_HW */

    // RAM / fake-driven suites — independent of any physical bus being present.
    run_group("system_event_log", &sentinel::test::system_event_log::run_all);
    run_group("device_snapshot", &sentinel::test::device_snapshot::run_all);
    run_group("POST", &sentinel::test::post::run_all);
    run_group("snapshot_stream", &sentinel::test::snapshot_stream::run_all);

    // -------- Per-group + overall summary --------
    banner("TEST SUMMARY");
    auto overall = sentinel::test::tally{};
    for (auto i = int{0}; i < count; ++i) {
        overall += groups[i].tally;
        cy_log_msg(CYLF_DEF, CY_LOG_INFO, "  %-20s %2u passed, %2u failed%s\n",
                   groups[i].name,
                   static_cast<unsigned>(groups[i].tally.passed),
                   static_cast<unsigned>(groups[i].tally.failed),
                   groups[i].tally.all_passed() ? "" : "   <-- FAIL");
    }
    rule();
    cy_log_msg(CYLF_DEF, CY_LOG_INFO, "  TOTAL: %u passed, %u failed\n",
               static_cast<unsigned>(overall.passed),
               static_cast<unsigned>(overall.failed));
    banner(overall.all_passed() ? "ALL TESTS PASSED" : "SOME TESTS FAILED");

    // -------- Hand off to the continuous readers --------
    // Only now — after every one-shot group has completed — start the ~1 Hz
    // serial readers, so their output can never interleave the diagnostic above
    // (#48 AC readers_start_after).
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "\nStarting continuous reader services "
               "(rtc_service, bme280_service)...\n");
    if (task::rtc_service::instance().task_create() != pdPASS) {
        loge("orchestrator: rtc_service create failed", "");
    }
    if (task::bme280_service::instance().task_create() != pdPASS) {
        loge("orchestrator: bme280_service create failed", "");
    }

    // One-shot: a FreeRTOS task must not fall off the end of its entry function
    // (that traps in prvTaskExitError with interrupts disabled), so delete it.
    m_handle = nullptr;
    vTaskDelete(nullptr);
}

} // namespace sentinel::testbench
