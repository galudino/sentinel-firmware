///
/// \file    sentinel_app_orchestrator.cpp
/// \brief   Production boot orchestrator task implementation (#38)
///
/// \details Implements \ref sentinel::app::boot_orchestrator. See the header and
///          decision #13 for the full rationale. The boot sequence here is the
///          real-hardware counterpart of #34/#35's off-bench testbenches: POST
///          probes the actual BME280 / DS3231 / W25Q128 through the shared device
///          context, and its results plus the boot-lifecycle records land in the
///          flash-backed System Event Log.
///
///          \b Event ordering. \c post::record_results enqueues the POST records
///          (non-blocking), then the event-log drain task is started: its
///          \c run() runs \c run_boot_sequence() FIRST — appending
///          \c shutdown_unexpected / \c boot_complete directly to flash while the
///          POST records are still queued — then block-drains the POST records.
///          Reading the prior session's last flash record \e before this boot's
///          POST records are persisted is what lets \c shutdown_unexpected carry
///          the correct prior-crash timestamp (decision #11). Consequence: the
///          POST records sit just after \c boot_complete rather than before it —
///          a deliberate deviation from decision #13's literal "POST is the first
///          writer" wording, in favour of accurate crash attribution.
///
/// \author  galudino
/// \date    2026-06-30
/// \version 1.0 - Initial production boot orchestrator implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_log.h"
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

#include "sentinel_app_orchestrator.hpp"

#include "sentinel_debug_print.hpp"
#include "sentinel_device_context.hpp"
#include "sentinel_post.hpp"

///< Service tasks the orchestrator spawns.
#include "sentinel_task_battery_service.hpp"
#include "sentinel_task_bme280_service.hpp"
#include "sentinel_task_rtc_service.hpp"
#include "sentinel_task_snapshot_persistence.hpp"
#include "sentinel_task_snapshot_stream.hpp"

#include <cstdint>

namespace sentinel::app {

namespace {

/// \brief First failed subsystem id (0 = all passed) for the snapshot field.
uint8_t first_failure_id(
    const sentinel::diagnostics::post::summary &s) noexcept {
    if (s.all_passed) {
        return 0u;
    }
    for (auto i = uint8_t{0}; i < s.count; i++) {
        if (s.results[i].result != sentinel::diagnostics::post_result::pass) {
            return static_cast<uint8_t>(s.results[i].subsystem);
        }
    }
    return 0u;
}

/// \brief Start a service task, logging on failure (boot continues regardless).
void start_task(const char *name, BaseType_t rc) noexcept {
    if (rc != pdPASS) {
        loge("orchestrator: %s create failed", name);
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "boot orchestrator: %s create failed\n", name);
    }
}

} // namespace

boot_orchestrator &boot_orchestrator::instance() noexcept {
    static boot_orchestrator the_instance;
    return the_instance;
}

BaseType_t boot_orchestrator::task_create(bool ble_stack_ok, bool gatt_db_ok,
                                          UBaseType_t priority,
                                          uint16_t stack_words) noexcept {
    m_ble_stack_ok = ble_stack_ok;
    m_gatt_db_ok   = gatt_db_ok;
    return xTaskCreate(&boot_orchestrator::task_trampoline, "Boot Orchestrator",
                       stack_words, this, priority, &m_handle);
}

void boot_orchestrator::task_trampoline(void *task_parameter) {
    static_cast<boot_orchestrator *>(task_parameter)->run();
}

void boot_orchestrator::run() {
    namespace res  = sentinel::resource;
    namespace diag = sentinel::diagnostics;

    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "\nboot orchestrator: starting boot sequence\n");

    // ---- 1. Build the shared device context + scan the flash stores. ----
    // First touch of context() constructs the drivers here, post-scheduler, so
    // the BME280 calibration read goes through the running I²C arbiter.
    auto &ctx = res::context();
    if (!res::initialize_stores()) {
        loge("orchestrator: flash store init failed (event/snapshot scan)", "");
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "boot orchestrator: flash store init failed\n");
        // Continue regardless — POST will record the record-store failure and
        // the device runs degraded (decision #12).
    }

    // ---- 2. POST against the real drivers; cache + record the results. ----
    const auto summary = diag::post::run(ctx.bme, ctx.rtc, ctx.flash,
                                         ctx.event_store, m_ble_stack_ok,
                                         m_gatt_db_ok);
    ctx.post_last_status = first_failure_id(summary);
    diag::post::record_results(ctx.event_log(), summary); // enqueues records

    // ---- 3. Start the event-log drain task. ----
    // Its run() executes run_boot_sequence() (boot-lifecycle records) first,
    // then block-drains the POST records enqueued above. See the file header on
    // ordering.
    start_task("event log", ctx.event_log().task_create());

    // ---- 4. Start the service tasks. ----
    start_task("rtc service", task::rtc_service::instance().task_create());
    start_task("bme280 service", task::bme280_service::instance().task_create());
    start_task("snapshot persistence",
               task::snapshot_persistence_task::instance().task_create());
    start_task("snapshot stream",
               task::snapshot_stream_task::instance().task_create());
    start_task("battery service",
               task::battery_service::instance().task_create());

    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "boot orchestrator: boot complete, %s\n",
               summary.all_passed ? "POST passed" : "POST reported failures");

    // ---- 5. One-shot: a FreeRTOS task must not return; delete self. ----
    m_handle = nullptr;
    vTaskDelete(nullptr);
}

} // namespace sentinel::app
