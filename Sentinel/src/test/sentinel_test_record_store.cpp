///
/// \file    sentinel_test_record_store.cpp
/// \brief   Flash-backed circular record store test suite implementation
///
/// \details Implements the run-to-completion testbench suite declared in
///          \c sentinel_test_record_store.hpp. The tests exercise the
///          \ref sentinel::record_store storage primitive (firmware #33)
///          against a physical W25Q128JV attached to
///          \c sentinel::resource::cybsp_spi via the bus arbiter
///          \c sentinel::resource::cybsp_spi_bus.
///
///          All tests operate on a dedicated scratch region near the top of
///          flash (\c kRegionOffset), two sectors wide. This is clear of
///          the \c sentinel::test::w25q128 scratch sector (\c 0xFFF000)
///          and of any plausible application data, so the suites do not
///          interfere with one another.
///
///          Structure (#48): the individual tests are members of a TU-local
///          \c fixture that owns the bus-arbitrated SPI transport, mirroring a
///          GoogleTest \c TEST_F fixture — the shared resource lives in the
///          fixture, not a file-static global. Each test returns \c true on
///          pass / \c false on fail; \ref sentinel::test::record_store::run_all
///          constructs the fixture, folds every outcome into a
///          \ref sentinel::test::tally, and returns it.
///
/// \author  galudino
/// \date    2026-06-28
/// \version 2.0 - Run-to-completion fixture suite (#48)
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_result.h"
#include "cycfg_pins.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_cyhal_spi_bus_transport.hpp"
#include "sentinel_debug_print.hpp"
#include "sentinel_record_store.hpp"
#include "sentinel_resource.hpp"
#include "sentinel_test_record_store.hpp"
#include "sentinel_test_result.hpp"
#include "sentinel_utilities.hpp"
#include "sentinel_w25q128.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

/// W25Q128 driver instantiated over the bus-arbitrated SPI transport.
using flash_t = sentinel::w25q128<sentinel::cyhal_spi_bus_transport>;

///
/// \brief Fixed-size payload used by the record_store tests.
///
/// \details 24 bytes (multiple of 4). With the store's 8-byte slot header
///          this yields a 32-byte slot, i.e. 128 records per 4 KiB sector.
///
struct test_record {
    uint32_t value;  ///< Seed value the record was built from.
    uint8_t tag[20]; ///< Bytes derived from \ref value for a memcmp check.
};
static_assert(sizeof(test_record) % 4 == 0, "test_record must be 4-aligned");

/// Circular record_store over \c test_record, backed by \ref flash_t.
using store_t =
    sentinel::record_store<test_record, sentinel::cyhal_spi_bus_transport>;

/// Scratch region: two sectors near the top of flash, clear of 0xFFF000.
constexpr uint32_t kRegionOffset = 0xF00000u;
constexpr uint32_t kRegionSize = 2u * flash_t::SECTOR_SIZE_BYTES; ///< 8 KiB.

///
/// \brief Yield long enough for the BLE debug ring buffer to drain.
///
/// \param milliseconds Delay duration, in milliseconds.
///
inline void yield_for_debug_drain(uint32_t milliseconds) noexcept {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

///
/// \brief Build a deterministic record from a seed value.
///
/// \param seed Seed value; becomes \c value and derives every \c tag byte.
/// \return The constructed \ref test_record.
///
inline test_record make_record(uint32_t seed) noexcept {
    auto r = test_record{};
    r.value = seed;
    for (auto i = uint32_t{0}; i < sizeof(r.tag); i++) {
        r.tag[i] = static_cast<uint8_t>(seed + i);
    }
    return r;
}

///
/// \brief Byte-for-byte record comparison.
///
/// \param a First record.
/// \param b Second record.
/// \return \c true if \p a and \p b are identical.
///
inline bool records_equal(const test_record &a, const test_record &b) noexcept {
    return a.value == b.value && std::memcmp(a.tag, b.tag, sizeof(a.tag)) == 0;
}

///
/// \brief Test fixture: owns the bus-arbitrated SPI transport every test shares.
///
/// \details Targets the same flash CS line (\c CYBSP_SPI_FLASH_CS / SS0) as the
///          W25Q128 driver suite. Constructed fresh by
///          \ref sentinel::test::record_store::run_all (like a
///          GoogleTest \c SetUp), so there is no file-static bus global. The
///          transport is inert until \c peripheral_initialize() has spawned the
///          arbiter, which the orchestrator guarantees by running post-scheduler.
///
struct fixture {
    /// Bus-arbitrated SPI transport shared by every test in this fixture.
    sentinel::cyhal_spi_bus_transport flash_bus{
        sentinel::resource::cybsp_spi_bus, CYBSP_SPI_FLASH_CS};

    /// \brief Fresh store reports empty.
    /// \return \c true on success.
    bool presence_check() noexcept;
    /// \brief Append one record, read it back.
    /// \return \c true on success.
    bool append_round_trip() noexcept;
    /// \brief Append 100 records, read them back in order.
    /// \return \c true on success.
    bool many_append() noexcept;
    /// \brief Fill to capacity + 1; the oldest record is overwritten.
    /// \return \c true on success.
    bool wrap_around() noexcept;
    /// \brief A partial (uncommitted) record is skipped by recovery scan.
    /// \return \c true on success.
    bool power_loss_simulation() noexcept;
    /// \brief A fresh store re-derives head/tail from on-flash state.
    /// \return \c true on success.
    bool survive_reset() noexcept;
    /// \brief A blank recycled sector 0 is not mistaken for a blank region;
    ///        recovery falls back to the full scan.
    /// \return \c true on success.
    bool recycle_transient_recovery() noexcept;
};

} // namespace

// ============================================================================
// fixture::presence_check
// ============================================================================

bool fixture::presence_check() noexcept {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    logi("record_store presence_check: SLOT_SIZE=%u capacity=%u",
         static_cast<unsigned>(store_t::SLOT_SIZE),
         static_cast<unsigned>(store.capacity()));
    yield_for_debug_drain(200);

    if (!store.erase_all()) {
        loge("presence_check FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        return false;
    }

    auto fresh = store_t(flash, kRegionOffset, kRegionSize);
    if (!fresh.initialize()) {
        loge("presence_check FAIL: initialize error %d",
             static_cast<int>(fresh.last_error()));
        return false;
    }

    if (fresh.count() != 0 || fresh.head_index() != 0 ||
        fresh.tail_index() != 0) {
        loge("presence_check FAIL: expected empty, got count=%u head=%u "
             "tail=%u",
             static_cast<unsigned>(fresh.count()),
             static_cast<unsigned>(fresh.head_index()),
             static_cast<unsigned>(fresh.tail_index()));
        return false;
    }

    logi("presence_check PASS: fresh store reports empty");
    return true;
}

// ============================================================================
// fixture::append_round_trip
// ============================================================================

bool fixture::append_round_trip() noexcept {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    yield_for_debug_drain(200);

    if (!store.erase_all()) {
        loge("append_round_trip FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        return false;
    }

    auto in = make_record(0x42u);
    if (!store.append(in)) {
        loge("append_round_trip FAIL: append error %d",
             static_cast<int>(store.last_error()));
        return false;
    }
    if (store.count() != 1) {
        loge("append_round_trip FAIL: count=%u expected 1",
             static_cast<unsigned>(store.count()));
        return false;
    }

    auto out = test_record{};
    if (!store.read(0, &out)) {
        loge("append_round_trip FAIL: read error %d",
             static_cast<int>(store.last_error()));
        return false;
    }
    if (!records_equal(in, out)) {
        loge("append_round_trip FAIL: readback mismatch (value=%u)",
             static_cast<unsigned>(out.value));
        return false;
    }

    logi("append_round_trip PASS: single record round-tripped");
    return true;
}

// ============================================================================
// fixture::many_append
// ============================================================================

bool fixture::many_append() noexcept {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    yield_for_debug_drain(200);

    constexpr uint32_t kCount = 100u;

    if (!store.erase_all()) {
        loge("many_append FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        return false;
    }

    for (auto i = uint32_t{0}; i < kCount; i++) {
        if (!store.append(make_record(1000u + i))) {
            loge("many_append FAIL: append %u error %d",
                 static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            return false;
        }
    }
    if (store.count() != kCount) {
        loge("many_append FAIL: count=%u expected %u",
             static_cast<unsigned>(store.count()),
             static_cast<unsigned>(kCount));
        return false;
    }

    for (auto i = uint32_t{0}; i < kCount; i++) {
        auto out = test_record{};
        if (!store.read(i, &out)) {
            loge("many_append FAIL: read %u error %d", static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            return false;
        }
        if (!records_equal(make_record(1000u + i), out)) {
            loge("many_append FAIL: record %u mismatch (value=%u)",
                 static_cast<unsigned>(i), static_cast<unsigned>(out.value));
            return false;
        }
    }

    logi("many_append PASS: %u records round-tripped in order",
         static_cast<unsigned>(kCount));
    return true;
}

// ============================================================================
// fixture::wrap_around
// ============================================================================

bool fixture::wrap_around() noexcept {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    yield_for_debug_drain(200);

    if (!store.erase_all()) {
        loge("wrap_around FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        return false;
    }

    const auto cap = store.capacity();

    // Fill exactly to capacity.
    for (auto i = uint32_t{0}; i < cap; i++) {
        if (!store.append(make_record(i))) {
            loge("wrap_around FAIL: fill append %u error %d",
                 static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            return false;
        }
    }
    if (store.count() != cap || store.tail_index() != 0) {
        loge("wrap_around FAIL: after fill count=%u tail=%u (cap=%u)",
             static_cast<unsigned>(store.count()),
             static_cast<unsigned>(store.tail_index()),
             static_cast<unsigned>(cap));
        return false;
    }

    // One more append forces a wrap: the head re-enters sector 0, which is
    // erased, and the tail advances past the records it destroyed.
    if (!store.append(make_record(cap))) {
        loge("wrap_around FAIL: wrap append error %d",
             static_cast<int>(store.last_error()));
        return false;
    }

    if (store.head_index() != cap + 1u || store.tail_index() == 0u) {
        loge("wrap_around FAIL: post-wrap head=%u tail=%u (expected head=%u, "
             "tail>0)",
             static_cast<unsigned>(store.head_index()),
             static_cast<unsigned>(store.tail_index()),
             static_cast<unsigned>(cap + 1u));
        return false;
    }

    // Newest record must still be readable; the oldest (index 0) must now be
    // out of range.
    auto out = test_record{};
    if (!store.read(cap, &out) || !records_equal(make_record(cap), out)) {
        loge("wrap_around FAIL: newest record not readable after wrap");
        return false;
    }
    if (store.read(0, &out)) {
        loge("wrap_around FAIL: overwritten record 0 still readable");
        return false;
    }

    // Recovery from flash must reproduce the in-RAM head/tail/count.
    auto recovered = store_t(flash, kRegionOffset, kRegionSize);
    if (!recovered.initialize() ||
        recovered.head_index() != store.head_index() ||
        recovered.tail_index() != store.tail_index() ||
        recovered.count() != store.count()) {
        loge("wrap_around FAIL: recovery mismatch (head %u/%u tail %u/%u)",
             static_cast<unsigned>(recovered.head_index()),
             static_cast<unsigned>(store.head_index()),
             static_cast<unsigned>(recovered.tail_index()),
             static_cast<unsigned>(store.tail_index()));
        return false;
    }

    logi("wrap_around PASS: wrapped at cap=%u, tail->%u, newest readable",
         static_cast<unsigned>(cap), static_cast<unsigned>(store.tail_index()));
    return true;
}

// ============================================================================
// fixture::power_loss_simulation
// ============================================================================

bool fixture::power_loss_simulation() noexcept {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    yield_for_debug_drain(200);

    constexpr uint32_t kCommitted = 10u;

    if (!store.erase_all()) {
        loge("power_loss FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        return false;
    }

    for (auto i = uint32_t{0}; i < kCommitted; i++) {
        if (!store.append(make_record(i))) {
            loge("power_loss FAIL: append %u error %d",
                 static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            return false;
        }
    }

    // Emulate a power loss between the payload write and the status commit:
    // the payload lands but the status byte stays 0xFF.
    if (!store.append_uncommitted_for_test(make_record(0xDEADu))) {
        loge("power_loss FAIL: uncommitted write error %d",
             static_cast<int>(store.last_error()));
        return false;
    }

    // Re-scan from flash (warm boot). The partial record must be skipped.
    auto recovered = store_t(flash, kRegionOffset, kRegionSize);
    if (!recovered.initialize()) {
        loge("power_loss FAIL: recovery initialize error %d",
             static_cast<int>(recovered.last_error()));
        return false;
    }

    if (recovered.count() != kCommitted ||
        recovered.head_index() != kCommitted) {
        loge("power_loss FAIL: partial record not skipped (count=%u head=%u)",
             static_cast<unsigned>(recovered.count()),
             static_cast<unsigned>(recovered.head_index()));
        return false;
    }

    auto out = test_record{};
    if (recovered.read(kCommitted, &out)) {
        loge("power_loss FAIL: partial record readable at index %u",
             static_cast<unsigned>(kCommitted));
        return false;
    }

    logi("power_loss PASS: partial record skipped, count=%u preserved",
         static_cast<unsigned>(kCommitted));
    return true;
}

// ============================================================================
// fixture::survive_reset
// ============================================================================

bool fixture::survive_reset() noexcept {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    yield_for_debug_drain(200);

    constexpr uint32_t kCount = 25u;
    constexpr uint32_t kBase = 7000u;

    if (!store.erase_all()) {
        loge("survive_reset FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        return false;
    }

    for (auto i = uint32_t{0}; i < kCount; i++) {
        if (!store.append(make_record(kBase + i))) {
            loge("survive_reset FAIL: append %u error %d",
                 static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            return false;
        }
    }

    // A fresh store over the same region emulates a warm boot: nothing in RAM
    // carries over; head/tail must be re-derived purely from on-flash state.
    auto rebooted = store_t(flash, kRegionOffset, kRegionSize);
    if (!rebooted.initialize()) {
        loge("survive_reset FAIL: re-init error %d",
             static_cast<int>(rebooted.last_error()));
        return false;
    }

    if (rebooted.count() != kCount || rebooted.head_index() != kCount) {
        loge("survive_reset FAIL: post-boot count=%u head=%u (expected %u)",
             static_cast<unsigned>(rebooted.count()),
             static_cast<unsigned>(rebooted.head_index()),
             static_cast<unsigned>(kCount));
        return false;
    }

    auto last = test_record{};
    auto first = test_record{};
    if (!rebooted.read(kCount - 1u, &last) ||
        !records_equal(make_record(kBase + kCount - 1u), last) ||
        !rebooted.read(0, &first) ||
        !records_equal(make_record(kBase), first)) {
        loge("survive_reset FAIL: post-boot read/verify failed");
        return false;
    }

    logi("survive_reset PASS: %u records recovered after warm boot",
         static_cast<unsigned>(kCount));
    return true;
}

// ============================================================================
// fixture::recycle_transient_recovery
// ============================================================================

bool fixture::recycle_transient_recovery() noexcept {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    yield_for_debug_drain(200);

    // Fill exactly to capacity so BOTH sectors hold valid records (slot 0 still
    // carries sequence 0 — the log has not wrapped yet).
    const auto cap = store.capacity();
    if (!store.erase_all()) {
        loge("recycle_transient FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        return false;
    }
    for (auto i = uint32_t{0}; i < cap; i++) {
        if (!store.append(make_record(i))) {
            loge("recycle_transient FAIL: fill append %u error %d",
                 static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            return false;
        }
    }

    // Simulate a power loss caught mid-recycle of sector 0: the driver erases
    // the whole sector before rewriting slot 0, so a crash in that window
    // leaves sector 0 blank (slot 0 EMPTY) while the later sector still holds
    // valid records. Reproduce it by erasing only the first sector directly.
    if (!flash.sector_erase_4kb(kRegionOffset)) {
        loge("recycle_transient FAIL: direct sector-0 erase error");
        return false;
    }

    const auto per_sector = store_t::RECORDS_PER_SECTOR;

    // Recovery must NOT mistake the blank sector 0 for a blank region and
    // discard the surviving records — it must fall back to the full scan.
    auto recovered = store_t(flash, kRegionOffset, kRegionSize);
    if (!recovered.initialize()) {
        loge("recycle_transient FAIL: initialize error %d",
             static_cast<int>(recovered.last_error()));
        return false;
    }

    if (recovered.count() != cap - per_sector ||
        recovered.tail_index() != per_sector ||
        recovered.head_index() != cap) {
        loge("recycle_transient FAIL: recovery mismatch count=%u tail=%u "
             "head=%u (expected count=%u tail=%u head=%u)",
             static_cast<unsigned>(recovered.count()),
             static_cast<unsigned>(recovered.tail_index()),
             static_cast<unsigned>(recovered.head_index()),
             static_cast<unsigned>(cap - per_sector),
             static_cast<unsigned>(per_sector), static_cast<unsigned>(cap));
        return false;
    }

    // The oldest surviving record (tail) and the newest must round-trip; the
    // erased records must now be out of range.
    auto out = test_record{};
    if (!recovered.read(per_sector, &out) ||
        !records_equal(make_record(per_sector), out) ||
        !recovered.read(cap - 1u, &out) ||
        !records_equal(make_record(cap - 1u), out)) {
        loge("recycle_transient FAIL: surviving records not readable");
        return false;
    }
    if (recovered.read(0, &out) || recovered.read(per_sector - 1u, &out)) {
        loge("recycle_transient FAIL: erased record still readable");
        return false;
    }

    logi("recycle_transient PASS: blank sector 0 not mistaken for blank region "
         "(%u records survived)",
         static_cast<unsigned>(recovered.count()));
    return true;
}

// ============================================================================
// sentinel::test::record_store::run_all
// ============================================================================

sentinel::test::tally sentinel::test::record_store::run_all() noexcept {
    auto fx = fixture{};
    auto t  = sentinel::test::tally{};

    t.record(fx.presence_check());
    yield_for_debug_drain(200);

    t.record(fx.append_round_trip());
    yield_for_debug_drain(200);

    t.record(fx.many_append());
    yield_for_debug_drain(200);

    t.record(fx.wrap_around());
    yield_for_debug_drain(200);

    t.record(fx.power_loss_simulation());
    yield_for_debug_drain(200);

    t.record(fx.survive_reset());
    yield_for_debug_drain(200);

    t.record(fx.recycle_transient_recovery());
    yield_for_debug_drain(200);

    return t;
}
