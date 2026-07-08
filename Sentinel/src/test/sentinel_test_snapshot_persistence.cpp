///
/// \file    sentinel_test_snapshot_persistence.cpp
/// \brief   Snapshot persistence task test suite implementation (lane 1, #38)
///
/// \details Implements the run-to-completion suite declared in
///          \c sentinel_test_snapshot_persistence.hpp. A TU-local \c fixture owns
///          a scratch \ref sentinel::resource::snapshot_store_t over two sectors
///          near the top of flash (\ref kRegionOffset) — clear of the
///          record_store suite's region (0xF00000), the w25q128 scratch sector
///          (0xFFF000), and the production event-log / snapshot regions low in
///          flash — and binds it to the real
///          \ref sentinel::task::snapshot_persistence_task singleton so every
///          test drives the production code path. The default shared-context
///          store is restored before \ref run_all returns.
///
/// \author  galudino
/// \date    2026-06-30
/// \version 1.0 - snapshot persistence task test suite
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cycfg_pins.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_cyhal_spi_bus_transport.hpp"
#include "sentinel_debug_print.hpp"
#include "sentinel_device_context.hpp"
#include "sentinel_device_snapshot.hpp"
#include "sentinel_record_store.hpp"
#include "sentinel_resource.hpp"
#include "sentinel_task_snapshot_persistence.hpp"
#include "sentinel_test_result.hpp"
#include "sentinel_test_snapshot_persistence.hpp"
#include "sentinel_w25q128.hpp"

#include <cstdint>

namespace {

using flash_t = sentinel::w25q128<sentinel::cyhal_spi_bus_transport>;
using store_t = sentinel::resource::snapshot_store_t;
using snapshot = sentinel::telemetry::device_snapshot;
using sentinel::task::snapshot_persistence_task;

/// Scratch region: two sectors above the record_store suite's 0xF00000 region.
constexpr uint32_t kRegionOffset = 0xF02000u;
constexpr uint32_t kRegionSize   = 2u * flash_t::SECTOR_SIZE_BYTES; // 8 KiB

/// \brief Yield long enough for the BLE debug ring buffer to drain.
inline void yield_for_debug_drain(uint32_t milliseconds) noexcept {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

/// \brief A captured snapshot is well-formed (written by populate_snapshot).
inline bool snapshot_well_formed(const snapshot &s) noexcept {
    return s.trailer_magic == sentinel::telemetry::SNAPSHOT_TRAILER_MAGIC &&
           s.snapshot_version == sentinel::telemetry::SNAPSHOT_VERSION;
}

///
/// \brief Fixture: owns the scratch store and binds it to the real task.
///
struct fixture {
    // The flash driver holds a reference to its transport, so the transport must
    // be a named member that outlives it (not a constructor temporary).
    sentinel::cyhal_spi_bus_transport flash_bus{sentinel::resource::cybsp_spi_bus,
                                                CYBSP_SPI_FLASH_CS};
    flash_t flash{flash_bus, sentinel::resource::flash_device_mutex};
    store_t store{flash, kRegionOffset, kRegionSize};

    fixture() noexcept {
        store.erase_all(); // also marks the store initialized
        snapshot_persistence_task::instance().bind_store(&store);
    }
    ~fixture() noexcept {
        // Restore the production default so nothing else picks up the scratch
        // store after the suite finishes.
        snapshot_persistence_task::instance().bind_store(nullptr);
    }

    bool presence_check() noexcept;
    bool capture_and_readback() noexcept;
    bool read_range_ordered() noexcept;
    bool capture_now_increments() noexcept;
    bool wrap_around() noexcept;
};

// ============================================================================
// fixture::presence_check
// ============================================================================

bool fixture::presence_check() noexcept {
    auto &task = snapshot_persistence_task::instance();
    logi("snapshot_persistence presence_check: SLOT_SIZE=%u capacity=%u",
         static_cast<unsigned>(store_t::SLOT_SIZE),
         static_cast<unsigned>(store.capacity()));

    if (!task.erase_all()) {
        loge("presence_check FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        return false;
    }
    if (task.count() != 0u) {
        loge("presence_check FAIL: count=%u expected 0",
             static_cast<unsigned>(task.count()));
        return false;
    }

    logi("presence_check PASS: fresh store empty");
    return true;
}

// ============================================================================
// fixture::capture_and_readback
// ============================================================================

bool fixture::capture_and_readback() noexcept {
    auto &task = snapshot_persistence_task::instance();
    if (!task.erase_all()) {
        return false;
    }

    constexpr uint32_t kCount = 3u;
    for (auto i = uint32_t{0}; i < kCount; i++) {
        if (!task.capture_now()) {
            loge("capture_and_readback FAIL: capture %u error %d",
                 static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            return false;
        }
    }

    if (task.count() != kCount) {
        loge("capture_and_readback FAIL: count=%u expected %u",
             static_cast<unsigned>(task.count()),
             static_cast<unsigned>(kCount));
        return false;
    }

    for (auto i = uint32_t{0}; i < kCount; i++) {
        auto s = snapshot{};
        if (!task.read(i, &s) || !snapshot_well_formed(s)) {
            loge("capture_and_readback FAIL: read/verify index %u",
                 static_cast<unsigned>(i));
            return false;
        }
    }

    logi("capture_and_readback PASS: %u snapshots round-tripped",
         static_cast<unsigned>(kCount));
    return true;
}

// ============================================================================
// fixture::read_range_ordered
// ============================================================================

bool fixture::read_range_ordered() noexcept {
    auto &task = snapshot_persistence_task::instance();
    if (!task.erase_all()) {
        return false;
    }

    constexpr uint32_t kCount = 4u;
    for (auto i = uint32_t{0}; i < kCount; i++) {
        if (!task.capture_now()) {
            return false;
        }
    }

    snapshot out[kCount]{};
    if (!task.read_range(0, kCount, out)) {
        loge("read_range_ordered FAIL: read_range error");
        return false;
    }

    // Every record well-formed; uptime never decreases across the run (captures
    // are monotonic in time even when the RTC has not yet ticked).
    for (auto i = uint32_t{0}; i < kCount; i++) {
        if (!snapshot_well_formed(out[i])) {
            loge("read_range_ordered FAIL: malformed at %u",
                 static_cast<unsigned>(i));
            return false;
        }
        if (i > 0 && out[i].uptime_seconds < out[i - 1].uptime_seconds) {
            loge("read_range_ordered FAIL: uptime regressed at %u",
                 static_cast<unsigned>(i));
            return false;
        }
    }

    logi("read_range_ordered PASS: %u snapshots in order",
         static_cast<unsigned>(kCount));
    return true;
}

// ============================================================================
// fixture::capture_now_increments
// ============================================================================

bool fixture::capture_now_increments() noexcept {
    auto &task = snapshot_persistence_task::instance();
    if (!task.erase_all()) {
        return false;
    }
    if (!task.capture_now() || !task.capture_now()) {
        return false;
    }

    const auto before = task.count();
    if (!task.capture_now()) {
        loge("capture_now_increments FAIL: capture error %d",
             static_cast<int>(store.last_error()));
        return false;
    }
    if (task.count() != before + 1u) {
        loge("capture_now_increments FAIL: count %u -> %u (expected +1)",
             static_cast<unsigned>(before),
             static_cast<unsigned>(task.count()));
        return false;
    }

    logi("capture_now_increments PASS: count %u -> %u",
         static_cast<unsigned>(before), static_cast<unsigned>(task.count()));
    return true;
}

// ============================================================================
// fixture::wrap_around
// ============================================================================

bool fixture::wrap_around() noexcept {
    auto &task = snapshot_persistence_task::instance();
    if (!task.erase_all()) {
        return false;
    }

    // Capture past capacity to force a sector recycle (oldest-overwritten-wins).
    const auto capacity = store.capacity();
    const auto total    = capacity + store_t::RECORDS_PER_SECTOR + 1u;
    for (auto i = uint32_t{0}; i < total; i++) {
        if (!task.capture_now()) {
            loge("wrap_around FAIL: capture %u error %d",
                 static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            return false;
        }
    }

    // After wrapping, count never exceeds capacity, the oldest valid record
    // (tail) is readable, and one below tail is gone.
    if (task.count() == 0u || task.count() > capacity) {
        loge("wrap_around FAIL: count=%u capacity=%u",
             static_cast<unsigned>(task.count()),
             static_cast<unsigned>(capacity));
        return false;
    }

    const auto tail = store.tail_index();
    const auto head = store.head_index();
    auto oldest = snapshot{};
    auto newest = snapshot{};
    if (!task.read(tail, &oldest) || !snapshot_well_formed(oldest) ||
        !task.read(head - 1u, &newest) || !snapshot_well_formed(newest)) {
        loge("wrap_around FAIL: tail/head read (tail=%u head=%u)",
             static_cast<unsigned>(tail), static_cast<unsigned>(head));
        return false;
    }

    auto evicted = snapshot{};
    if (tail > 0u && task.read(tail - 1u, &evicted)) {
        loge("wrap_around FAIL: evicted record %u still readable",
             static_cast<unsigned>(tail - 1u));
        return false;
    }

    logi("wrap_around PASS: count=%u tail=%u head=%u",
         static_cast<unsigned>(task.count()), static_cast<unsigned>(tail),
         static_cast<unsigned>(head));
    return true;
}

} // namespace

// ============================================================================
// sentinel::test::snapshot_persistence::run_all
// ============================================================================

sentinel::test::tally
sentinel::test::snapshot_persistence::run_all() noexcept {
    auto fx = fixture{};
    auto t  = sentinel::test::tally{};

    t.record(fx.presence_check());
    yield_for_debug_drain(200);

    t.record(fx.capture_and_readback());
    yield_for_debug_drain(200);

    t.record(fx.read_range_ordered());
    yield_for_debug_drain(200);

    t.record(fx.capture_now_increments());
    yield_for_debug_drain(200);

    t.record(fx.wrap_around());
    yield_for_debug_drain(200);

    return t;
}
