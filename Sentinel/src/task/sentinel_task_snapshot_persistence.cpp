///
/// \file    sentinel_task_snapshot_persistence.cpp
/// \brief   Periodic device_snapshot flash persistence task impl (lane 1, #38)
///
/// \details Implements the persistence task declared in
///          \c sentinel_task_snapshot_persistence.hpp. The task loops
///          \c populate_snapshot() → \c store.append() at the configured
///          cadence;
///          \c populate_snapshot() is cache-backed (decision #14) so the only
///          bus traffic on this path is the flash write itself, serialized with
///          the event-log writer by the shared W25Q128 device mutex (decision
///          #4). The first capture lands immediately on task start, giving the
///          history a "first snapshot of this boot" anchor aligned with the
///          event log's \c boot_complete (issue #38 implementation note).
///
/// \author  galudino
/// \date    2026-06-30
/// \version 1.0 - Initial snapshot persistence task implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_log.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_debug_print.hpp"
#include "sentinel_device_context.hpp"
#include "sentinel_device_snapshot.hpp"
#include "sentinel_task_snapshot_persistence.hpp"

#include <cstdint>

namespace {

/// \brief Cadence floor. At 1 s the flash wear is still ~20-year endurance
///        (issue #38 wear budget); below that only burns flash for no gain.
constexpr uint32_t MIN_PERIOD_SECONDS = 1;

} // namespace

using namespace sentinel::task;

snapshot_persistence_task &snapshot_persistence_task::instance() noexcept {
    static snapshot_persistence_task task;
    return task;
}

sentinel::resource::snapshot_store_t &
snapshot_persistence_task::store() const noexcept {
    return m_store != nullptr ? *m_store
                              : sentinel::resource::context().snapshot_store;
}

void snapshot_persistence_task::bind_store(
    sentinel::resource::snapshot_store_t *store) noexcept {
    m_store = store;
}

bool snapshot_persistence_task::set_period_seconds(
    uint32_t period_seconds) noexcept {
    m_period_seconds = period_seconds < MIN_PERIOD_SECONDS ? MIN_PERIOD_SECONDS
                                                           : period_seconds;
    return true;
}

uint32_t snapshot_persistence_task::period_seconds() const noexcept {
    return m_period_seconds;
}

uint32_t snapshot_persistence_task::count() const noexcept {
    return store().count();
}

bool snapshot_persistence_task::read(
    uint32_t index, sentinel::telemetry::device_snapshot *out) const noexcept {
    return store().read(index, out);
}

bool snapshot_persistence_task::read_range(
    uint32_t start, uint32_t n,
    sentinel::telemetry::device_snapshot *out) const noexcept {
    if (out == nullptr) {
        return false;
    }
    for (auto i = uint32_t{0}; i < n; i++) {
        if (!store().read(start + i, &out[i])) {
            return false;
        }
    }
    return true;
}

bool snapshot_persistence_task::erase_all() noexcept {
    return store().erase_all();
}

bool snapshot_persistence_task::capture_now() noexcept {
    auto snap = sentinel::telemetry::device_snapshot{};
    sentinel::telemetry::populate_snapshot(&snap);

    if (!store().append(snap)) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "Snapshot persistence: append failed (store err=%d)\n",
                   static_cast<int>(store().last_error()));

        loge("snapshot_persistence: append failed (store err=%d)",
             static_cast<int>(store().last_error()));
        return false;
    }

    ++m_capture_count;

    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "Snapshot persistence: captured #%u (store count=%u)\n",
               static_cast<unsigned>(m_capture_count),
               static_cast<unsigned>(store().count()));

    logi("snapshot_persistence: captured #%u (store count=%u)",
         static_cast<unsigned>(m_capture_count),
         static_cast<unsigned>(store().count()));

    // Heartbeat only on the production (context-store) path: it references the
    // shared event log, which pairs with the context store — not a bound test
    // store. Gated on context_ready so a pre-orchestrator caller is a no-op.
    if (m_store == nullptr && (m_capture_count % HEARTBEAT_EVERY_N == 0) &&
        sentinel::resource::context_ready()) {
        sentinel::resource::context().event_log().record_snapshot_persisted(
            store().count());
    }

    return true;
}

BaseType_t
snapshot_persistence_task::task_create(UBaseType_t priority,
                                       uint16_t stack_words,
                                       uint32_t period_seconds) noexcept {
    set_period_seconds(period_seconds);
    return xTaskCreate(&snapshot_persistence_task::task_trampoline,
                       "Snapshot Persistence Task", stack_words, this, priority,
                       &m_handle);
}

void snapshot_persistence_task::task_trampoline(void *task_parameter) {
    static_cast<snapshot_persistence_task *>(task_parameter)->run();
}

void snapshot_persistence_task::run() {
    logi("snapshot_persistence: capturing every %d s",
         static_cast<int>(m_period_seconds));
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "Snapshot persistence: capturing every %d s\n",
               static_cast<int>(m_period_seconds));

    while (true) {
        // Capture first so the very first snapshot is the boot anchor, then
        // wait out the cadence. Re-reading m_period_seconds each pass picks up
        // a runtime set_period_seconds() change.
        capture_now();
        vTaskDelay(pdMS_TO_TICKS(m_period_seconds * 1000u));
    }
}
