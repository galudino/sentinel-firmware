///
/// \file    sentinel_boot_orchestrator.cpp
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
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

#include "sentinel_boot_orchestrator.hpp"

#include "sentinel_debug_print.hpp"
#include "sentinel_orchestrator_entry.hpp"
#include "sentinel_device_context.hpp"
#include "sentinel_gatt_dis.hpp"
#include "sentinel_gatt_snapshot_stream.hpp"
#include "sentinel_gatt_system.hpp"
#include "sentinel_platform_id.hpp"
#include "sentinel_post.hpp"

///< Service tasks the orchestrator spawns.
#include "sentinel_task_battery_service.hpp"
#include "sentinel_task_ble_maintenance.hpp"
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

/// \brief Human-readable POST subsystem name for the serial / BLE feedback.
const char *subsystem_name(sentinel::diagnostics::post_subsystem s) noexcept {
    using ps = sentinel::diagnostics::post_subsystem;
    switch (s) {
    case ps::bme280:         return "bme280";
    case ps::ds3231:         return "ds3231";
    case ps::w25q128:        return "w25q128";
    case ps::record_store:   return "record_store";
    case ps::ble_stack:      return "ble_stack";
    case ps::rotary_encoder: return "rotary_encoder";
    case ps::display:        return "display";
    case ps::invalid:        return "invalid";
    }
    return "?";
}

/// \brief Human-readable POST result code (PASS or the failure reason).
const char *result_name(sentinel::diagnostics::post_result r) noexcept {
    using pr = sentinel::diagnostics::post_result;
    switch (r) {
    case pr::pass:           return "PASS";
    case pr::fail_no_ack:    return "fail_no_ack";
    case pr::fail_wrong_id:  return "fail_wrong_id";
    case pr::fail_self_test: return "fail_self_test";
    case pr::fail_timeout:   return "fail_timeout";
    case pr::fail_init:      return "fail_init";
    }
    return "?";
}

/// \brief Start a service task, logging on failure (boot continues regardless).
void start_task(const char *name, BaseType_t rc) noexcept {
    if (rc != pdPASS) {
        loge("boot: %s task create failed", name);
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

    logi("boot: starting sequence");

    // ---- 1. Build the shared device context + scan the flash stores. ----
    // First touch of context() constructs the drivers here, post-scheduler, so
    // the BME280 calibration read goes through the running I²C arbiter. The two
    // region scans are O(capacity) (~8 k SPI reads each — slow; see issue #49),
    // so the progress lines below matter: without them a healthy boot looks hung.
    logi("boot: building device context...");
    auto &ctx = res::context();
    logi("boot: device context built (BME280 init err=%d)",
         static_cast<int>(ctx.bme.last_error()));

    logi("boot: scanning event-log region (%u slots)...",
         static_cast<unsigned>(ctx.event_store.capacity()));
    const auto event_ok = ctx.event_store.initialize();
    logi("boot: event log ready (ok=%d, %u records)",
         static_cast<int>(event_ok),
         static_cast<unsigned>(ctx.event_store.count()));

    logi("boot: scanning snapshot region (%u slots)...",
         static_cast<unsigned>(ctx.snapshot_store.capacity()));
    const auto snapshot_ok = ctx.snapshot_store.initialize();
    logi("boot: snapshot history ready (ok=%d, %u records)",
         static_cast<int>(snapshot_ok),
         static_cast<unsigned>(ctx.snapshot_store.count()));

    const auto log_ok = res::event_log_t::instance().initialize(
        ctx.event_store, &res::now_unix_seconds);
    res::g_context_ready = event_ok && snapshot_ok && log_ok;
    if (!res::g_context_ready) {
        loge("boot: flash store init failed (event/snapshot scan)");
        // Continue regardless — POST will record the record-store failure and
        // the device runs degraded (decision #12).
    }

    // ---- 2. POST against the real drivers; log + record each probe. ----
    // probe_record_store now reuses the already-initialized event store (no
    // redundant rescan), so POST is fast and the per-probe lines print promptly.
    logi("---- [ POST ] ----");
    // Time only the probe phase (no interleaved logging inside run()), so the
    // reported duration is the true POST timing for #35's "< 100 ms" hardware AC
    // — the multi-second flash scans above are NOT part of POST.
    const auto post_start_ticks = xTaskGetTickCount();
    const auto summary = diag::post::run(ctx.bme, ctx.rtc, ctx.flash,
                                         ctx.event_store, m_ble_stack_ok,
                                         m_gatt_db_ok);
    const auto post_ms = static_cast<unsigned>(
        (xTaskGetTickCount() - post_start_ticks) * portTICK_PERIOD_MS);
    for (auto i = uint8_t{0}; i < summary.count; ++i) {
        const auto &r = summary.results[i];
        logi("post: %s %s", subsystem_name(r.subsystem), result_name(r.result));
    }
    logi("---- [ POST ] done in %u ms: %s ----", post_ms,
         summary.all_passed ? "all subsystems passed" : "failures recorded");
    ctx.post_last_status = first_failure_id(summary);
    diag::post::record_results(ctx.event_log(), summary); // enqueues records

    // ---- 3. Start the event-log drain task. ----
    // Its run() executes run_boot_sequence() (boot-lifecycle records) first,
    // then block-drains the POST records enqueued above. See the file header on
    // ordering.
    start_task("event log", ctx.event_log().task_create());

    // ---- 4a. Seed the System service + Device Information Service (#6/#45). ----
    // The GATT DB value arrays live in RAM independent of registration; seed the
    // machine-stable identity now (before a central connects) so first reads are
    // correct. Manufacturer Name is derived from vendor_of(platform) (#45); the
    // DIS Firmware Revision / Serial mirror the System values.
    {
        namespace gsys = sentinel::gatt::system;
        const auto platform = sentinel::current_platform_id();
        gsys::set_firmware_version(sentinel::current_firmware_version);
        gsys::set_platform_id(platform);
        sentinel::gatt::dis::populate(platform, sentinel::current_firmware_version,
                                      gsys::serial_number());
        logi("boot: GATT identity seeded (platform=%u, serial=%lu)",
             static_cast<unsigned>(sentinel::to_underlying(platform)),
             static_cast<unsigned long>(gsys::serial_number()));
    }

    // ---- 4. Start the service tasks. ----
    logi("boot: starting service tasks...");
    start_task("rtc service", task::rtc_service::instance().task_create());
    start_task("bme280 service", task::bme280_service::instance().task_create());
    start_task("snapshot persistence",
               task::snapshot_persistence_task::instance().task_create());
    // Attach the live snapshot stream (#46, lane 2) to its GATT notify sink (#6)
    // before starting it, so the Snapshot Notify Enable characteristic can drive
    // start()/stop() and each produced snapshot lands on the Current Device
    // Snapshot characteristic.
    task::snapshot_stream_task::instance().set_notify_sink(
        &sentinel::gatt::snapshot_stream::notify_sink);
    start_task("snapshot stream",
               task::snapshot_stream_task::instance().task_create());
    start_task("battery service",
               task::battery_service::instance().task_create());
    // Async handler for slow BLE-triggered maintenance (store clears + deferred
    // bootloader reset), kept off the Bluetooth callback (#6).
    start_task("ble maintenance",
               task::ble_maintenance_task::instance().task_create());

    logi("boot: complete (%s)",
         summary.all_passed ? "POST passed" : "POST reported failures");

    // ---- 5. One-shot: a FreeRTOS task must not return; delete self. ----
    m_handle = nullptr;
    vTaskDelete(nullptr);
}

} // namespace sentinel::app

// Per-target entry symbol (#51): the shared main.cpp calls this; the linker
// resolves it to whichever orchestrator TU is compiled (this one when the app
// target is built, the testbench's when TESTBENCH=1).
namespace sentinel {
BaseType_t create_orchestrator(bool ble_stack_ok, bool gatt_db_ok) noexcept {
    return app::boot_orchestrator::instance().task_create(ble_stack_ok,
                                                          gatt_db_ok);
}
} // namespace sentinel
