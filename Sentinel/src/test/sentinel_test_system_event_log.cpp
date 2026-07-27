///
/// \file    sentinel_test_system_event_log.cpp
/// \brief   System Event Log test implementations
///
/// \details Implements the tests declared in
///          \c sentinel_test_system_event_log.hpp. The System Event Log
///          (firmware #34) is exercised against the RAM-backed
///          \ref sentinel::ram_record_store so the suite needs no physical
///          flash and produces deterministic timestamps via an injected clock.
///
///          Backing buffers are taken from the FreeRTOS heap with
///          \c pvPortMalloc and freed after each test. The largest
///          (\c record_burst, 1000 records) needs ~44 KiB, which is sized to
///          live in the heap rather than as a static .bss array that would eat
///          the non-heap SRAM headroom.
///
///          Each test drives the drain path synchronously with
///          \c drain_pending(); the production drain task runs the same code.
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - System Event Log test implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_debug_print.hpp"
#include "sentinel_firmware_version.hpp"
#include "sentinel_ram_record_store.hpp"
#include "sentinel_system_event.hpp"
#include "sentinel_system_event_log.hpp"
#include "sentinel_test_result.hpp"
#include "sentinel_test_system_event_log.hpp"

#include <cstdint>
#include <cstring>

namespace {

using namespace sentinel::diagnostics;

/// RAM-backed record_store over a \c system_event_record.
using store_t = sentinel::ram_record_store<system_event_record>;
/// System event log templated over \ref store_t.
using log_t = sentinel::diagnostics::system_event_log<store_t>;

/// Injected, controllable clock so timestamps are deterministic in tests.
uint32_t g_test_clock = 0u;
/// \brief Deterministic clock callback injected into \ref log_t::initialize.
/// \return The current value of \ref g_test_clock.
uint32_t test_now() noexcept { return g_test_clock; }

///
/// \brief Allocate a RAM backing buffer for \p records slots from the heap.
///
/// \param records Number of slots the buffer must hold.
/// \return Pointer to the buffer, or \c nullptr on allocation failure.
///
uint8_t *alloc_buffer(uint32_t records) noexcept {
    return static_cast<uint8_t *>(pvPortMalloc(records * store_t::SLOT_SIZE));
}

///
/// \brief Report a single test's PASS/FAIL to both the BLE stream and UART.
///
/// \param name   Test name.
/// \param ok     \c true if the test passed.
/// \param detail Failure reason (ignored when \p ok is \c true).
///
void report(const char *name, bool ok, const char *detail) noexcept {
    if (ok) {
        logi("%s PASS", name);
    } else {
        loge("%s FAIL: %s", name, detail);
    }
}

/// \brief Yield long enough for the BLE debug ring buffer to drain.
/// \param milliseconds Delay duration, in milliseconds.
void yield_for_debug_drain(uint32_t milliseconds) noexcept {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

// ---------------------------------------------------------------------------
// Test bodies: return true on success, write a short reason to *why on failure.
// The public wrappers own buffer lifetime + reporting.
// ---------------------------------------------------------------------------

/// \brief \c initialize() on a fresh store succeeds and \c count() == 0.
/// \param buf  Backing buffer for the RAM record store.
/// \param size Size of \p buf, in bytes.
/// \param why  Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_presence_check(uint8_t *buf, uint32_t size, const char **why) {
    auto store = store_t(buf, size);
    if (!store.erase_all()) {
        *why = "erase_all";
        return false;
    }
    auto &log = log_t::instance();
    if (!log.initialize(store, test_now)) {
        *why = "initialize";
        return false;
    }
    if (log.count() != 0u) {
        *why = "count != 0";
        return false;
    }
    return true;
}

/// \brief Boot sequence yields one readable \c boot_complete with a non-zero
///        timestamp.
/// \param buf  Backing buffer for the RAM record store.
/// \param size Size of \p buf, in bytes.
/// \param why  Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_record_and_read(uint8_t *buf, uint32_t size, const char **why) {
    auto store = store_t(buf, size);
    auto &log = log_t::instance();
    if (!store.erase_all() || !log.initialize(store, test_now)) {
        *why = "setup";
        return false;
    }

    g_test_clock = 1000u;
    if (!log.run_boot_sequence()) {
        *why = "boot sequence";
        return false;
    }
    if (log.count() != 1u) {
        *why = "count != 1";
        return false;
    }

    auto rec = log.read(0);
    if (!rec) {
        *why = "read(0)";
        return false;
    }
    if (rec->header.event_type != system_event::boot_complete) {
        *why = "not boot_complete";
        return false;
    }
    if (rec->header.unix_timestamp == 0u) {
        *why = "zero timestamp";
        return false;
    }
    return true;
}

/// \brief A \c firmware_update_record survives memcpy through the untyped
///        store and back.
/// \param buf  Backing buffer for the RAM record store.
/// \param size Size of \p buf, in bytes.
/// \param why  Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_typed_round_trip(uint8_t *buf, uint32_t size, const char **why) {
    auto store = store_t(buf, size);
    auto &log = log_t::instance();
    if (!store.erase_all() || !log.initialize(store, test_now)) {
        *why = "setup";
        return false;
    }

    g_test_clock = 2000u;
    const auto from = sentinel::firmware_version(1, 0, 0, 1);
    const auto to = sentinel::firmware_version(1, 2, 3, 7);
    if (!log.record_firmware_update(system_event::firmware_update_completed,
                                    from, to, 0xABu)) {
        *why = "record";
        return false;
    }
    if (log.drain_pending() != 1u) {
        *why = "drain != 1";
        return false;
    }

    auto rec = log.read(0);
    if (!rec) {
        *why = "read(0)";
        return false;
    }
    auto fur = firmware_update_record{};
    std::memcpy(&fur, &*rec, sizeof(fur));
    if (fur.from_version.major != 1u || fur.from_version.build != 1u ||
        fur.to_version.minor != 2u || fur.to_version.patch != 3u ||
        fur.to_version.build != 7u || fur.mcuboot_result != 0xABu) {
        *why = "field mismatch";
        return false;
    }
    return true;
}

/// \brief 1000 mixed events all persist.
/// \param buf  Backing buffer for the RAM record store.
/// \param size Size of \p buf, in bytes.
/// \param why  Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_record_burst(uint8_t *buf, uint32_t size, const char **why) {
    auto store = store_t(buf, size);
    auto &log = log_t::instance();
    if (!store.erase_all() || !log.initialize(store, test_now)) {
        *why = "setup";
        return false;
    }

    constexpr uint32_t kCount = 1000u;
    g_test_clock = 3000u;

    for (auto i = uint32_t{0}; i < kCount; i++) {
        const auto emit = [&]() -> bool {
            return (i & 1u)
                       ? log.record_mode_change(static_cast<uint8_t>(i),
                                                static_cast<uint8_t>(i + 1u),
                                                0u)
                       : log.record_post_passed();
        };
        // Non-blocking queue is shallow; drain to relieve backpressure.
        while (!emit()) {
            log.drain_pending();
        }
    }
    log.drain_pending();

    if (log.count() != kCount) {
        *why = "count != 1000";
        return false;
    }

    // Spot-check the alternating event types persisted in order.
    auto r0 = log.read(0);
    if (!r0 || r0->header.event_type != system_event::post_passed) {
        *why = "record 0 wrong";
        return false;
    }
    auto r1 = log.read(1);
    if (!r1 || r1->header.event_type != system_event::mode_changed) {
        *why = "record 1 wrong";
        return false;
    }
    return true;
}

/// \brief Records and ordering survive a simulated warm reboot; boot adds
///        exactly one record.
/// \param buf  Backing buffer for the RAM record store.
/// \param size Size of \p buf, in bytes.
/// \param why  Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_survive_reset(uint8_t *buf, uint32_t size, const char **why) {
    auto store = store_t(buf, size);
    auto &log = log_t::instance();
    if (!store.erase_all() || !log.initialize(store, test_now)) {
        *why = "setup";
        return false;
    }

    constexpr uint32_t kEvents = 20u;
    g_test_clock = 7000u;
    if (!log.run_boot_sequence()) { // count -> 1
        *why = "boot";
        return false;
    }
    for (auto i = uint32_t{0}; i < kEvents; i++) {
        if (!log.record_fault(static_cast<uint8_t>(i), 1u, 2u, nullptr) ||
            log.drain_pending() != 1u) {
            *why = "record fault";
            return false;
        }
    }
    // Clean shutdown so the reboot adds exactly one record (no synthesis).
    if (!log.record_shutdown_clean(sentinel::current_firmware_version, 1u,
                                   123u) ||
        log.drain_pending() != 1u) {
        *why = "shutdown_clean";
        return false;
    }
    const auto pre = log.count();

    // Simulate a warm reboot: re-scan the same buffer, rebind, re-boot.
    auto rebooted = store_t(buf, size);
    if (!rebooted.initialize()) {
        *why = "reboot init";
        return false;
    }
    g_test_clock = 8000u;
    if (!log.initialize(rebooted, test_now) || !log.run_boot_sequence()) {
        *why = "reboot boot";
        return false;
    }
    if (log.count() != pre + 1u) {
        *why = "count != pre+1";
        return false;
    }

    // Original records intact and in order.
    auto rec0 = log.read(0);
    if (!rec0 || rec0->header.event_type != system_event::boot_complete) {
        *why = "record 0";
        return false;
    }
    for (auto i = uint32_t{0}; i < kEvents; i++) {
        auto rec = log.read(1u + i);
        if (!rec || rec->header.event_type != system_event::fault_raised) {
            *why = "fault order";
            return false;
        }
        auto fr = fault_record{};
        std::memcpy(&fr, &*rec, sizeof(fr));
        if (fr.fault_id != static_cast<uint8_t>(i)) {
            *why = "fault id";
            return false;
        }
    }
    auto rec_shutdown = log.read(1u + kEvents);
    if (!rec_shutdown ||
        rec_shutdown->header.event_type != system_event::shutdown_clean) {
        *why = "shutdown order";
        return false;
    }
    return true;
}

/// \brief An unclean reboot synthesizes a \c shutdown_unexpected for the
///        prior session.
/// \param buf  Backing buffer for the RAM record store.
/// \param size Size of \p buf, in bytes.
/// \param why  Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_unexpected_shutdown_synthesis(uint8_t *buf, uint32_t size,
                                        const char **why) {
    auto store = store_t(buf, size);
    auto &log = log_t::instance();
    if (!store.erase_all() || !log.initialize(store, test_now)) {
        *why = "setup";
        return false;
    }

    g_test_clock = 5000u;
    if (!log.run_boot_sequence() || log.count() != 1u) { // boot_complete bc=1
        *why = "first boot";
        return false;
    }

    // Reboot WITHOUT a clean shutdown event.
    auto rebooted = store_t(buf, size);
    if (!rebooted.initialize()) {
        *why = "reboot init";
        return false;
    }
    g_test_clock = 6000u;
    if (!log.initialize(rebooted, test_now) || !log.run_boot_sequence()) {
        *why = "reboot boot";
        return false;
    }

    // Expect: boot_complete(@5000,bc1), shutdown_unexpected(@5000),
    //         boot_complete(@6000,bc2).
    if (log.count() != 3u) {
        *why = "count != 3";
        return false;
    }
    auto r1 = log.read(1);
    if (!r1 || r1->header.event_type != system_event::shutdown_unexpected ||
        r1->header.unix_timestamp != 5000u) {
        *why = "no synthesized shutdown";
        return false;
    }
    auto r2 = log.read(2);
    if (!r2 || r2->header.event_type != system_event::boot_complete) {
        *why = "no fresh boot";
        return false;
    }
    auto blr = boot_lifecycle_record{};
    std::memcpy(&blr, &*r2, sizeof(blr));
    if (blr.boot_count != 2u) {
        *why = "boot_count != 2";
        return false;
    }
    return true;
}

/// \brief Erase resets count to 0 and new records still append.
/// \param buf  Backing buffer for the RAM record store.
/// \param size Size of \p buf, in bytes.
/// \param why  Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_erase_all(uint8_t *buf, uint32_t size, const char **why) {
    auto store = store_t(buf, size);
    auto &log = log_t::instance();
    if (!store.erase_all() || !log.initialize(store, test_now)) {
        *why = "setup";
        return false;
    }

    g_test_clock = 9000u;
    log.run_boot_sequence();
    if (!log.record_post_passed()) {
        *why = "record";
        return false;
    }
    log.drain_pending();
    if (log.count() == 0u) {
        *why = "nothing recorded";
        return false;
    }

    if (!log.erase_all() || log.count() != 0u) {
        *why = "erase_all";
        return false;
    }

    if (!log.record_post_passed() || log.drain_pending() != 1u ||
        log.count() != 1u) {
        *why = "append after erase";
        return false;
    }
    return true;
}

/// \brief Wrap overwrites the oldest record, keeps the newest readable.
/// \param buf  Backing buffer for the RAM record store.
/// \param size Size of \p buf, in bytes.
/// \param why  Set to a short failure reason when returning \c false.
/// \return \c true on success.
bool body_crossing_size_threshold(uint8_t *buf, uint32_t size,
                                  const char **why) {
    auto store = store_t(buf, size);
    auto &log = log_t::instance();
    if (!store.erase_all() || !log.initialize(store, test_now)) {
        *why = "setup";
        return false;
    }

    const auto cap = store.capacity();
    g_test_clock = 10000u;
    for (auto i = uint32_t{0}; i < cap; i++) {
        if (!log.record_post_passed() || log.drain_pending() != 1u) {
            *why = "fill";
            return false;
        }
    }
    if (log.count() != cap) {
        *why = "not full";
        return false;
    }

    // One more forces a wrap: oldest overwritten, count stays at capacity.
    if (!log.record_mode_change(1u, 2u, 0u) || log.drain_pending() != 1u) {
        *why = "wrap append";
        return false;
    }
    if (log.count() != cap) {
        *why = "count changed";
        return false;
    }

    auto newest = log.read(cap);
    if (!newest || newest->header.event_type != system_event::mode_changed) {
        *why = "newest unreadable";
        return false;
    }
    if (log.read(0)) {
        *why = "oldest still readable";
        return false;
    }
    return true;
}

///
/// \brief Run one test: allocate the buffer, run \p body, free, report.
///
/// \param name    Test name, used for the PASS/FAIL log line.
/// \param records Number of record slots to allocate for the backing buffer.
/// \param body    Test body; returns \c true on success and sets \c *why on
///                failure.
/// \return \c true if \p body passed; \c false on failure or if the backing
///         buffer could not be allocated.
///
bool run_one(const char *name, uint32_t records,
             bool (*body)(uint8_t *, uint32_t, const char **)) noexcept {
    auto *buf = alloc_buffer(records);
    if (buf == nullptr) {
        report(name, false, "buffer alloc");
        return false;
    }
    const char *why = "assertion";
    const auto ok = body(buf, records * store_t::SLOT_SIZE, &why);
    vPortFree(buf);
    report(name, ok, why);
    return ok;
}

} // namespace

// ============================================================================
// sentinel::test::system_event_log::run_all
// ============================================================================

sentinel::test::tally sentinel::test::system_event_log::run_all() noexcept {
    auto t = sentinel::test::tally{};

    t.record(run_one("presence_check", 8u, body_presence_check));
    yield_for_debug_drain(200);

    t.record(run_one("record_and_read", 8u, body_record_and_read));
    yield_for_debug_drain(200);

    t.record(run_one("typed_round_trip", 8u, body_typed_round_trip));
    yield_for_debug_drain(200);

    // 1024 slots ( > 1000 ) so the burst never wraps.
    t.record(run_one("record_burst", 1024u, body_record_burst));
    yield_for_debug_drain(200);

    t.record(run_one("survive_reset", 64u, body_survive_reset));
    yield_for_debug_drain(200);

    t.record(run_one("unexpected_shutdown_synthesis", 16u,
                     body_unexpected_shutdown_synthesis));
    yield_for_debug_drain(200);

    t.record(run_one("erase_all", 16u, body_erase_all));
    yield_for_debug_drain(200);

    t.record(
        run_one("crossing_size_threshold", 16u, body_crossing_size_threshold));
    yield_for_debug_drain(200);

    return t;
}
