///
/// \file    sentinel_test_w25q128.cpp
/// \brief   W25Q128 flash driver test suite implementation
///
/// \details Implements the run-to-completion testbench suite declared in
///          \c sentinel_test_w25q128.hpp. The tests exercise every public
///          member of \ref sentinel::w25q128 against a physical W25Q128JV
///          attached to \c sentinel::resource::cybsp_spi via the bus arbiter
///          \c sentinel::resource::cybsp_spi_bus.
///
///          Structure (#48): the individual tests are members of a TU-local
///          \c fixture that owns the bus-arbitrated SPI transport, mirroring a
///          GoogleTest \c TEST_F fixture — the shared resource lives in the
///          fixture, not a file-static global. Each test returns \c true on
///          pass / \c false on fail; \ref sentinel::test::w25q128::run_all
///          constructs the fixture, folds every outcome into a
///          \ref sentinel::test::tally, and returns it.
///
/// \author  galudino
/// \date    2026-05-18
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
#include "sentinel_resource.hpp"
#include "sentinel_span.hpp"
#include "sentinel_test_result.hpp"
#include "sentinel_test_w25q128.hpp"
#include "sentinel_utilities.hpp"
#include "sentinel_w25q128.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace {

///
/// \brief Yield long enough for the BLE debug ring buffer to drain.
///
/// \param milliseconds Yield duration in milliseconds.
///
inline void yield_for_debug_drain(uint32_t milliseconds) noexcept {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

///
/// \brief Convenience alias for the concrete driver instantiation.
///
using w25q128_t = sentinel::w25q128<sentinel::cyhal_spi_bus_transport>;

///
/// \brief Read a region back in chunks and confirm every byte is blank
///        (\c 0xFF).
///
/// \param flash   Driver instance to read through.
/// \param address Starting byte address of the region to check.
/// \param length  Number of bytes to check.
/// \return \c true if every byte in the region reads as \c 0xFF.
///
inline bool region_is_blank(w25q128_t &flash, uint32_t address,
                            uint32_t length) noexcept {
    auto buf = std::array<uint8_t, 64>{};
    auto remaining = length;
    auto offset = uint32_t{0};

    while (remaining > 0) {
        auto chunk =
            (remaining < buf.size()) ? remaining : uint32_t{buf.size()};

        if (!flash.read_data(address + offset,
                             sentinel::make_span(buf.data(), chunk))) {
            return false;
        }

        for (auto i = uint32_t{0}; i < chunk; i++) {
            if (buf[i] != 0xFF) {
                return false;
            }
        }

        offset += chunk;
        remaining -= chunk;
    }
    return true;
}

///
/// \brief Test fixture: owns the bus-arbitrated SPI transport every test
/// shares.
///
/// \details Targets the SCB's flash chip-select line (\c CYBSP_SPI_FLASH_CS).
///          Constructed fresh by \ref sentinel::test::w25q128::run_all (like a
///          GoogleTest \c SetUp), so there is no file-static bus global. The
///          transport is inert until
///          \c peripheral_initialize() has spawned the arbiter, which the
///          orchestrator guarantees by running post-scheduler. When
///          BME280-on-SPI lands in issue #2, that suite will own its own
///          \c cyhal_spi_bus_transport with SS1.
///
struct fixture {
    /// Bus-arbitrated SPI transport, shared by every test below.
    sentinel::cyhal_spi_bus_transport w25q128_bus{
        sentinel::resource::cybsp_spi_bus, CYBSP_SPI_FLASH_CS};

    /// \brief Read JEDEC / manufacturer-device / unique IDs and confirm
    ///        the JEDEC triple is in the known-good list.
    /// \return \c true if the JEDEC ID matches a known-good entry.
    bool presence_check() noexcept;
    /// \brief Flip an SR1 bit via a volatile write, read it back, restore it.
    /// \return \c true if the writable bits round-trip correctly.
    bool status_register_round_trip() noexcept;
    /// \brief Erase the last sector, verify blank, program a pattern, verify.
    /// \return \c true if every step succeeds and the readback matches.
    bool erase_program_read() noexcept;
    /// \brief Erase security register 3, verify blank, program, verify,
    ///        then erase again to leave it clean.
    /// \return \c true if the programmed pattern round-trips correctly.
    bool security_register_round_trip() noexcept;
    /// \brief Enter deep power-down, probe unresponsiveness, then release
    ///        and confirm full responsiveness.
    /// \return \c true if release yields the expected device ID and a
    ///         subsequent JEDEC read matches a known-good entry.
    bool power_down_release() noexcept;
};

} // namespace

// ============================================================================
// fixture::presence_check
// ============================================================================

bool fixture::presence_check() noexcept {
    auto flash = w25q128_t(w25q128_bus, sentinel::resource::flash_device_mutex);
    logi("W25Q128 presence_check: driver constructed");
    yield_for_debug_drain(200);

    auto jedec = flash.jedec_id();

    if (!jedec) {
        loge("presence_check FAIL: JEDEC read transport error %d",
             static_cast<int>(flash.last_error()));
        return false;
    }

    logi("presence_check: JEDEC = 0x%02X 0x%02X 0x%02X",
         static_cast<int>(jedec->manufacturer),
         static_cast<int>(jedec->memory_type),
         static_cast<int>(jedec->capacity));

    if (!w25q128_t::is_known_jedec(*jedec)) {
        loge("presence_check FAIL: JEDEC 0x%02X 0x%02X 0x%02X not in "
             "known-good list",
             static_cast<int>(jedec->manufacturer),
             static_cast<int>(jedec->memory_type),
             static_cast<int>(jedec->capacity));
        return false;
    }

    // Bonus diagnostics — manufacturer+device ID and unique ID.
    if (auto mfr_dev = flash.manufacturer_device_id()) {
        logi("presence_check: Mfr/Dev = 0x%02X 0x%02X",
             static_cast<int>(mfr_dev->manufacturer),
             static_cast<int>(mfr_dev->device));
    } else {
        logw("presence_check: Mfr/Dev read transport error %d",
             static_cast<int>(flash.last_error()));
    }

    yield_for_debug_drain(100);

    if (auto uid = flash.unique_id()) {
        logi("presence_check: Unique ID = %02X%02X%02X%02X%02X%02X%02X%02X",
             static_cast<int>((*uid)[0]), static_cast<int>((*uid)[1]),
             static_cast<int>((*uid)[2]), static_cast<int>((*uid)[3]),
             static_cast<int>((*uid)[4]), static_cast<int>((*uid)[5]),
             static_cast<int>((*uid)[6]), static_cast<int>((*uid)[7]));
    } else {
        logw("presence_check: Unique ID read transport error %d",
             static_cast<int>(flash.last_error()));
    }

    logi("W25Q128 presence_check PASS: JEDEC 0x%02X 0x%02X 0x%02X",
         static_cast<int>(jedec->manufacturer),
         static_cast<int>(jedec->memory_type),
         static_cast<int>(jedec->capacity));
    return true;
}

// ============================================================================
// fixture::status_register_round_trip
// ============================================================================

bool fixture::status_register_round_trip() noexcept {
    auto flash = w25q128_t(w25q128_bus, sentinel::resource::flash_device_mutex);
    logi("W25Q128 status_register_round_trip: driver constructed");
    yield_for_debug_drain(200);

    auto original = flash.read_status_register_1();
    if (!original) {
        loge("status_round_trip FAIL: initial SR1 read error %d",
             static_cast<int>(flash.last_error()));
        return false;
    }
    logi("status_round_trip: original SR1 = 0x%02X",
         static_cast<int>(*original));

    // Flip SR1 bit 6 (SEC) which only affects how BP0..2 are interpreted —
    // harmless on its own when BP bits are zero. Volatile write so any
    // failure does not persistently change the chip.
    auto target = static_cast<uint8_t>(
        *original ^ (1u << w25q128_t::status_register_1::SEC_BIT));
    if (!flash.write_status_register_1(target, /*volatile_only=*/true)) {
        loge("status_round_trip FAIL: SR1 write error %d",
             static_cast<int>(flash.last_error()));
        return false;
    }

    auto readback = flash.read_status_register_1();
    if (!readback) {
        loge("status_round_trip FAIL: SR1 readback error %d",
             static_cast<int>(flash.last_error()));
        return false;
    }

    // The volatile mask cannot affect read-only bits (BUSY, WEL); compare
    // only writable bits to avoid spurious mismatch.
    constexpr uint8_t writable_mask = 0xFCu; // bits 2..7 are writable
    auto ok = bool{};
    if ((static_cast<uint8_t>(*readback) & writable_mask) !=
        (target & writable_mask)) {
        loge("status_round_trip FAIL: readback 0x%02X != target 0x%02X",
             static_cast<int>(*readback), static_cast<int>(target));
        ok = false;
    } else {
        logi("status_round_trip PASS: SR1 round-tripped 0x%02X -> 0x%02X",
             static_cast<int>(*original), static_cast<int>(*readback));
        ok = true;
    }

    // Restore original (volatile write again).
    if (!flash.write_status_register_1(*original, /*volatile_only=*/true)) {
        logw("status_round_trip: restore error %d",
             static_cast<int>(flash.last_error()));
    }

    return ok;
}

// ============================================================================
// fixture::erase_program_read
// ============================================================================

bool fixture::erase_program_read() noexcept {
    auto flash = w25q128_t(w25q128_bus, sentinel::resource::flash_device_mutex);
    logi("W25Q128 erase_program_read: driver constructed");
    yield_for_debug_drain(200);

    // Test region: last sector of flash (0xFFF000), well above any
    // application data. Pattern: 256 bytes of incrementing values.
    constexpr uint32_t test_address = 0xFFF000u;
    constexpr uint32_t verify_length = 256u;
    auto pattern = std::array<uint8_t, verify_length>{};
    for (auto i = uint32_t{0}; i < verify_length; i++) {
        pattern[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // 1. Erase the sector.
    logi("erase_program_read: erasing sector at 0x%06X",
         static_cast<unsigned>(test_address));
    if (!flash.sector_erase_4kb(test_address)) {
        loge("erase_program_read FAIL: sector_erase error %d",
             static_cast<int>(flash.last_error()));
        return false;
    }
    yield_for_debug_drain(50);

    // 2. Verify blank.
    if (!region_is_blank(flash, test_address, verify_length)) {
        loge("erase_program_read FAIL: post-erase region not blank "
             "(last_err=%d)",
             static_cast<int>(flash.last_error()));
        return false;
    }
    logi("erase_program_read: post-erase region is blank");

    // 3. Program the page.
    if (!flash.page_program(
            test_address,
            sentinel::make_cspan(pattern.data(), pattern.size()))) {
        loge("erase_program_read FAIL: page_program error %d",
             static_cast<int>(flash.last_error()));
        return false;
    }
    logi("erase_program_read: programmed 256 bytes");

    // 4. Read back and verify.
    auto readback = std::array<uint8_t, verify_length>{};
    if (!flash.read_data(test_address, sentinel::make_span(readback.data(),
                                                           readback.size()))) {
        loge("erase_program_read FAIL: read_data error %d",
             static_cast<int>(flash.last_error()));
        return false;
    }

    for (auto i = uint32_t{0}; i < verify_length; i++) {
        if (readback[i] != pattern[i]) {
            loge("erase_program_read FAIL: byte %u readback=0x%02X "
                 "expected=0x%02X",
                 static_cast<unsigned>(i), static_cast<int>(readback[i]),
                 static_cast<int>(pattern[i]));
            return false;
        }
    }

    logi("erase_program_read PASS: 256 bytes round-tripped at 0x%06X",
         static_cast<unsigned>(test_address));
    return true;
}

// ============================================================================
// fixture::security_register_round_trip
// ============================================================================

bool fixture::security_register_round_trip() noexcept {
    auto flash = w25q128_t(w25q128_bus, sentinel::resource::flash_device_mutex);
    logi("W25Q128 security_register_round_trip: driver constructed");
    yield_for_debug_drain(200);

    constexpr uint8_t reg_index = 3; // least likely to collide with future use.

    // 1. Erase.
    if (!flash.erase_security_register(reg_index)) {
        loge("security_round_trip FAIL: erase reg %u error %d",
             static_cast<unsigned>(reg_index),
             static_cast<int>(flash.last_error()));
        return false;
    }

    // 2. Confirm blank — read the first 16 bytes.
    auto blank_check = std::array<uint8_t, 16>{};
    if (!flash.read_security_register(
            reg_index, 0,
            sentinel::make_span(blank_check.data(), blank_check.size()))) {
        loge("security_round_trip FAIL: post-erase read error %d",
             static_cast<int>(flash.last_error()));
        return false;
    }
    for (auto i = size_t{0}; i < blank_check.size(); i++) {
        if (blank_check[i] != 0xFF) {
            loge("security_round_trip FAIL: post-erase byte %u = 0x%02X",
                 static_cast<unsigned>(i), static_cast<int>(blank_check[i]));
            return false;
        }
    }

    // 3. Program a known pattern at offset 0.
    auto pattern =
        std::array<uint8_t, 16>{0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
                                0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    if (!flash.program_security_register(
            reg_index, 0,
            sentinel::make_cspan(pattern.data(), pattern.size()))) {
        loge("security_round_trip FAIL: program error %d",
             static_cast<int>(flash.last_error()));
        return false;
    }

    // 4. Read back and verify.
    auto readback = std::array<uint8_t, 16>{};
    if (!flash.read_security_register(
            reg_index, 0,
            sentinel::make_span(readback.data(), readback.size()))) {
        loge("security_round_trip FAIL: readback error %d",
             static_cast<int>(flash.last_error()));
        return false;
    }
    for (auto i = size_t{0}; i < pattern.size(); i++) {
        if (readback[i] != pattern[i]) {
            loge("security_round_trip FAIL: byte %u readback=0x%02X "
                 "expected=0x%02X",
                 static_cast<unsigned>(i), static_cast<int>(readback[i]),
                 static_cast<int>(pattern[i]));
            return false;
        }
    }

    // 5. Erase again so the security register is left clean for next run.
    if (!flash.erase_security_register(reg_index)) {
        logw("security_round_trip: final cleanup erase error %d",
             static_cast<int>(flash.last_error()));
    }

    logi("security_round_trip PASS: security reg %u round-tripped 16 bytes",
         static_cast<unsigned>(reg_index));
    return true;
}

// ============================================================================
// fixture::power_down_release
// ============================================================================

bool fixture::power_down_release() noexcept {
    auto flash = w25q128_t(w25q128_bus, sentinel::resource::flash_device_mutex);
    logi("W25Q128 power_down_release: driver constructed");
    yield_for_debug_drain(200);

    // 1. Enter deep power-down.
    if (!flash.power_down()) {
        loge("power_down_release FAIL: power_down error %d",
             static_cast<int>(flash.last_error()));
        return false;
    }

    // tDP (CS high to power-down) = 3 µs typical. Wait a bit longer.
    vTaskDelay(pdMS_TO_TICKS(1));

    // 2. Probe with JEDEC — chip should be unresponsive (no known-good ID).
    auto probe = flash.jedec_id();
    auto in_pd = !probe || !w25q128_t::is_known_jedec(*probe);
    if (!in_pd) {
        logw("power_down_release: chip still answering JEDEC during PD "
             "(some W25Q variants ignore commands silently rather than "
             "returning garbage — non-fatal)");
    } else {
        logi("power_down_release: confirmed unresponsive during PD");
    }

    // 3. Release power-down and read device ID.
    auto device_id = flash.release_power_down_device_id();
    if (!device_id) {
        loge("power_down_release FAIL: release/device_id error %d",
             static_cast<int>(flash.last_error()));
        return false;
    }
    if (*device_id != w25q128_t::RELEASE_POWER_DOWN_DEVICE_ID) {
        loge("power_down_release FAIL: device_id 0x%02X != expected 0x%02X",
             static_cast<int>(*device_id),
             static_cast<int>(w25q128_t::RELEASE_POWER_DOWN_DEVICE_ID));
        return false;
    }
    logi("power_down_release: release returned device_id 0x%02X",
         static_cast<int>(*device_id));

    // 4. JEDEC again to confirm full responsiveness.
    auto recheck = flash.jedec_id();
    if (!recheck || !w25q128_t::is_known_jedec(*recheck)) {
        loge("power_down_release FAIL: post-release JEDEC mismatch");
        return false;
    }

    logi("power_down_release PASS: PD -> release -> JEDEC round-trip OK");
    return true;
}

// ============================================================================
// sentinel::test::w25q128::run_all
// ============================================================================

sentinel::test::tally sentinel::test::w25q128::run_all() noexcept {
    auto fx = fixture{};
    auto t = sentinel::test::tally{};

    t.record(fx.presence_check());
    yield_for_debug_drain(200);

    t.record(fx.status_register_round_trip());
    yield_for_debug_drain(200);

    t.record(fx.erase_program_read());
    yield_for_debug_drain(200);

    t.record(fx.security_register_round_trip());
    yield_for_debug_drain(200);

    t.record(fx.power_down_release());
    yield_for_debug_drain(200);

    return t;
}
