///
/// \file    sentinel_test_record_store.cpp
/// \brief   Flash-backed circular record store test implementations
///
/// \details Implements the testbench tests declared in
///          \c sentinel_test_record_store.hpp. The tests exercise the
///          \ref sentinel::record_store storage primitive (firmware #33)
///          against a physical W25Q128JV attached to
///          \c sentinel::resource::cybsp_spi via the bus arbiter
///          \c sentinel::resource::cybsp_spi_bus.
///
///          All tests operate on a dedicated scratch region near the top of
///          flash (\ref kRegionOffset), two sectors wide. This is clear of
///          the \ref sentinel::test::w25q128 scratch sector (\c 0xFFF000)
///          and of any plausible application data, so the suites do not
///          interfere with one another.
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - record_store test implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_log.h"
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
#include "sentinel_utilities.hpp"
#include "sentinel_w25q128.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

///
/// \brief Bus-arbitrated SPI transport for the record_store test.
///
/// \details Constructed once at TU scope, targeting the same flash CS line
///          (\c CYBSP_SPI_FLASH_CS / SS0) as the W25Q128 driver test.
///
sentinel::cyhal_spi_bus_transport flash_bus{sentinel::resource::cybsp_spi_bus,
                                            CYBSP_SPI_FLASH_CS};

using flash_t = sentinel::w25q128<sentinel::cyhal_spi_bus_transport>;

///
/// \brief Fixed-size payload used by the record_store tests.
///
/// \details 24 bytes (multiple of 4). With the store's 8-byte slot header
///          this yields a 32-byte slot, i.e. 128 records per 4 KiB sector.
///
struct test_record {
    uint32_t value;
    uint8_t tag[20];
};
static_assert(sizeof(test_record) % 4 == 0, "test_record must be 4-aligned");

using store_t =
    sentinel::record_store<test_record, sentinel::cyhal_spi_bus_transport>;

/// Scratch region: two sectors near the top of flash, clear of 0xFFF000.
constexpr uint32_t kRegionOffset = 0xF00000u;
constexpr uint32_t kRegionSize = 2u * flash_t::SECTOR_SIZE_BYTES; // 8 KiB

///
/// \brief Yield long enough for the BLE debug ring buffer to drain.
///
inline void yield_for_debug_drain(uint32_t milliseconds) noexcept {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

///
/// \brief Build a deterministic record from a seed value.
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
inline bool records_equal(const test_record &a, const test_record &b) noexcept {
    return a.value == b.value && std::memcmp(a.tag, b.tag, sizeof(a.tag)) == 0;
}

} // namespace

// ============================================================================
// sentinel::test::record_store::all
// ============================================================================

void sentinel::test::record_store::all() {
    presence_check();
    yield_for_debug_drain(200);

    append_round_trip();
    yield_for_debug_drain(200);

    many_append();
    yield_for_debug_drain(200);

    wrap_around();
    yield_for_debug_drain(200);

    power_loss_simulation();
    yield_for_debug_drain(200);

    survive_reset();
    yield_for_debug_drain(200);

    logi("record_store: all tests complete", "");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO, "record_store: all tests complete\n");
}

// ============================================================================
// presence_check
// ============================================================================

void sentinel::test::record_store::presence_check() {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    logi("record_store presence_check: SLOT_SIZE=%u capacity=%u",
         static_cast<unsigned>(store_t::SLOT_SIZE),
         static_cast<unsigned>(store.capacity()));
    yield_for_debug_drain(200);

    if (!store.erase_all()) {
        loge("presence_check FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store presence_check FAIL: erase_all %d\n",
                   static_cast<int>(store.last_error()));
        return;
    }

    auto fresh = store_t(flash, kRegionOffset, kRegionSize);
    if (!fresh.initialize()) {
        loge("presence_check FAIL: initialize error %d",
             static_cast<int>(fresh.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store presence_check FAIL: initialize %d\n",
                   static_cast<int>(fresh.last_error()));
        return;
    }

    if (fresh.count() != 0 || fresh.head_index() != 0 ||
        fresh.tail_index() != 0) {
        loge("presence_check FAIL: expected empty, got count=%u head=%u "
             "tail=%u",
             static_cast<unsigned>(fresh.count()),
             static_cast<unsigned>(fresh.head_index()),
             static_cast<unsigned>(fresh.tail_index()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store presence_check FAIL: not empty\n");
        return;
    }

    logi("presence_check PASS: fresh store reports empty", "");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO, "record_store presence_check PASS\n");
}

// ============================================================================
// append_round_trip
// ============================================================================

void sentinel::test::record_store::append_round_trip() {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    yield_for_debug_drain(200);

    if (!store.erase_all()) {
        loge("append_round_trip FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store append_round_trip FAIL: erase_all %d\n",
                   static_cast<int>(store.last_error()));
        return;
    }

    auto in = make_record(0x42u);
    if (!store.append(in)) {
        loge("append_round_trip FAIL: append error %d",
             static_cast<int>(store.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store append_round_trip FAIL: append %d\n",
                   static_cast<int>(store.last_error()));
        return;
    }
    if (store.count() != 1) {
        loge("append_round_trip FAIL: count=%u expected 1",
             static_cast<unsigned>(store.count()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store append_round_trip FAIL: count != 1\n");
        return;
    }

    auto out = test_record{};
    if (!store.read(0, &out)) {
        loge("append_round_trip FAIL: read error %d",
             static_cast<int>(store.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store append_round_trip FAIL: read %d\n",
                   static_cast<int>(store.last_error()));
        return;
    }
    if (!records_equal(in, out)) {
        loge("append_round_trip FAIL: readback mismatch (value=%u)",
             static_cast<unsigned>(out.value));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store append_round_trip FAIL: mismatch\n");
        return;
    }

    logi("append_round_trip PASS: single record round-tripped", "");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO, "record_store append_round_trip PASS\n");
}

// ============================================================================
// many_append
// ============================================================================

void sentinel::test::record_store::many_append() {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    yield_for_debug_drain(200);

    constexpr uint32_t kCount = 100u;

    if (!store.erase_all()) {
        loge("many_append FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store many_append FAIL: erase_all %d\n",
                   static_cast<int>(store.last_error()));
        return;
    }

    for (auto i = uint32_t{0}; i < kCount; i++) {
        if (!store.append(make_record(1000u + i))) {
            loge("many_append FAIL: append %u error %d",
                 static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                       "record_store many_append FAIL: append %u\n",
                       static_cast<unsigned>(i));
            return;
        }
    }
    if (store.count() != kCount) {
        loge("many_append FAIL: count=%u expected %u",
             static_cast<unsigned>(store.count()),
             static_cast<unsigned>(kCount));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store many_append FAIL: count mismatch\n");
        return;
    }

    for (auto i = uint32_t{0}; i < kCount; i++) {
        auto out = test_record{};
        if (!store.read(i, &out)) {
            loge("many_append FAIL: read %u error %d", static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                       "record_store many_append FAIL: read %u\n",
                       static_cast<unsigned>(i));
            return;
        }
        if (!records_equal(make_record(1000u + i), out)) {
            loge("many_append FAIL: record %u mismatch (value=%u)",
                 static_cast<unsigned>(i), static_cast<unsigned>(out.value));
            cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                       "record_store many_append FAIL: mismatch at %u\n",
                       static_cast<unsigned>(i));
            return;
        }
    }

    logi("many_append PASS: %u records round-tripped in order",
         static_cast<unsigned>(kCount));
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "record_store many_append PASS: %u records\n",
               static_cast<unsigned>(kCount));
}

// ============================================================================
// wrap_around
// ============================================================================

void sentinel::test::record_store::wrap_around() {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    yield_for_debug_drain(200);

    if (!store.erase_all()) {
        loge("wrap_around FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store wrap_around FAIL: erase_all %d\n",
                   static_cast<int>(store.last_error()));
        return;
    }

    const auto cap = store.capacity();

    // Fill exactly to capacity.
    for (auto i = uint32_t{0}; i < cap; i++) {
        if (!store.append(make_record(i))) {
            loge("wrap_around FAIL: fill append %u error %d",
                 static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                       "record_store wrap_around FAIL: fill %u\n",
                       static_cast<unsigned>(i));
            return;
        }
    }
    if (store.count() != cap || store.tail_index() != 0) {
        loge("wrap_around FAIL: after fill count=%u tail=%u (cap=%u)",
             static_cast<unsigned>(store.count()),
             static_cast<unsigned>(store.tail_index()),
             static_cast<unsigned>(cap));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store wrap_around FAIL: bad full state\n");
        return;
    }

    // One more append forces a wrap: the head re-enters sector 0, which is
    // erased, and the tail advances past the records it destroyed.
    if (!store.append(make_record(cap))) {
        loge("wrap_around FAIL: wrap append error %d",
             static_cast<int>(store.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store wrap_around FAIL: wrap append %d\n",
                   static_cast<int>(store.last_error()));
        return;
    }

    if (store.head_index() != cap + 1u || store.tail_index() == 0u) {
        loge("wrap_around FAIL: post-wrap head=%u tail=%u (expected head=%u, "
             "tail>0)",
             static_cast<unsigned>(store.head_index()),
             static_cast<unsigned>(store.tail_index()),
             static_cast<unsigned>(cap + 1u));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store wrap_around FAIL: tail did not advance\n");
        return;
    }

    // Newest record must still be readable; the oldest (index 0) must now be
    // out of range.
    auto out = test_record{};
    if (!store.read(cap, &out) || !records_equal(make_record(cap), out)) {
        loge("wrap_around FAIL: newest record not readable after wrap", "");
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store wrap_around FAIL: newest unreadable\n");
        return;
    }
    if (store.read(0, &out)) {
        loge("wrap_around FAIL: overwritten record 0 still readable", "");
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store wrap_around FAIL: stale record readable\n");
        return;
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
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store wrap_around FAIL: recovery mismatch\n");
        return;
    }

    logi("wrap_around PASS: wrapped at cap=%u, tail->%u, newest readable",
         static_cast<unsigned>(cap), static_cast<unsigned>(store.tail_index()));
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "record_store wrap_around PASS: cap=%u tail=%u\n",
               static_cast<unsigned>(cap),
               static_cast<unsigned>(store.tail_index()));
}

// ============================================================================
// power_loss_simulation
// ============================================================================

void sentinel::test::record_store::power_loss_simulation() {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    yield_for_debug_drain(200);

    constexpr uint32_t kCommitted = 10u;

    if (!store.erase_all()) {
        loge("power_loss FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store power_loss FAIL: erase_all %d\n",
                   static_cast<int>(store.last_error()));
        return;
    }

    for (auto i = uint32_t{0}; i < kCommitted; i++) {
        if (!store.append(make_record(i))) {
            loge("power_loss FAIL: append %u error %d",
                 static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                       "record_store power_loss FAIL: append %u\n",
                       static_cast<unsigned>(i));
            return;
        }
    }

    // Emulate a power loss between the payload write and the status commit:
    // the payload lands but the status byte stays 0xFF.
    if (!store.append_uncommitted_for_test(make_record(0xDEADu))) {
        loge("power_loss FAIL: uncommitted write error %d",
             static_cast<int>(store.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store power_loss FAIL: uncommitted write %d\n",
                   static_cast<int>(store.last_error()));
        return;
    }

    // Re-scan from flash (warm boot). The partial record must be skipped.
    auto recovered = store_t(flash, kRegionOffset, kRegionSize);
    if (!recovered.initialize()) {
        loge("power_loss FAIL: recovery initialize error %d",
             static_cast<int>(recovered.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store power_loss FAIL: recovery init %d\n",
                   static_cast<int>(recovered.last_error()));
        return;
    }

    if (recovered.count() != kCommitted ||
        recovered.head_index() != kCommitted) {
        loge("power_loss FAIL: partial record not skipped (count=%u head=%u)",
             static_cast<unsigned>(recovered.count()),
             static_cast<unsigned>(recovered.head_index()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store power_loss FAIL: partial not skipped\n");
        return;
    }

    auto out = test_record{};
    if (recovered.read(kCommitted, &out)) {
        loge("power_loss FAIL: partial record readable at index %u",
             static_cast<unsigned>(kCommitted));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store power_loss FAIL: partial readable\n");
        return;
    }

    logi("power_loss PASS: partial record skipped, count=%u preserved",
         static_cast<unsigned>(kCommitted));
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "record_store power_loss PASS: count=%u preserved\n",
               static_cast<unsigned>(kCommitted));
}

// ============================================================================
// survive_reset
// ============================================================================

void sentinel::test::record_store::survive_reset() {
    auto flash = flash_t(flash_bus, sentinel::resource::flash_device_mutex);
    auto store = store_t(flash, kRegionOffset, kRegionSize);
    yield_for_debug_drain(200);

    constexpr uint32_t kCount = 25u;
    constexpr uint32_t kBase = 7000u;

    if (!store.erase_all()) {
        loge("survive_reset FAIL: erase_all error %d",
             static_cast<int>(store.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store survive_reset FAIL: erase_all %d\n",
                   static_cast<int>(store.last_error()));
        return;
    }

    for (auto i = uint32_t{0}; i < kCount; i++) {
        if (!store.append(make_record(kBase + i))) {
            loge("survive_reset FAIL: append %u error %d",
                 static_cast<unsigned>(i),
                 static_cast<int>(store.last_error()));
            cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                       "record_store survive_reset FAIL: append %u\n",
                       static_cast<unsigned>(i));
            return;
        }
    }

    // A fresh store over the same region emulates a warm boot: nothing in RAM
    // carries over; head/tail must be re-derived purely from on-flash state.
    auto rebooted = store_t(flash, kRegionOffset, kRegionSize);
    if (!rebooted.initialize()) {
        loge("survive_reset FAIL: re-init error %d",
             static_cast<int>(rebooted.last_error()));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store survive_reset FAIL: re-init %d\n",
                   static_cast<int>(rebooted.last_error()));
        return;
    }

    if (rebooted.count() != kCount || rebooted.head_index() != kCount) {
        loge("survive_reset FAIL: post-boot count=%u head=%u (expected %u)",
             static_cast<unsigned>(rebooted.count()),
             static_cast<unsigned>(rebooted.head_index()),
             static_cast<unsigned>(kCount));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store survive_reset FAIL: bad post-boot state\n");
        return;
    }

    auto last = test_record{};
    auto first = test_record{};
    if (!rebooted.read(kCount - 1u, &last) ||
        !records_equal(make_record(kBase + kCount - 1u), last) ||
        !rebooted.read(0, &first) ||
        !records_equal(make_record(kBase), first)) {
        loge("survive_reset FAIL: post-boot read/verify failed", "");
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "record_store survive_reset FAIL: post-boot read\n");
        return;
    }

    logi("survive_reset PASS: %u records recovered after warm boot",
         static_cast<unsigned>(kCount));
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "record_store survive_reset PASS: %u records\n",
               static_cast<unsigned>(kCount));
}

// ============================================================================
// task_create
// ============================================================================

BaseType_t sentinel::test::record_store::task_create() {
    constexpr auto stack_words = configMINIMAL_STACK_SIZE * 4;
    constexpr auto priority =
        static_cast<UBaseType_t>(configMAX_PRIORITIES - 3);

    return xTaskCreate(
        [](void *) -> void { sentinel::test::record_store::all(); },
        "Record Store Test Task", stack_words, nullptr, priority, nullptr);
}
