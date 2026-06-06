///
/// \file    sentinel_test_w25q128.cpp
/// \brief   W25Q128 flash driver test implementations
///
/// \details Implements the testbench smoke tests declared in
///          \c sentinel_test_w25q128.hpp. The tests exercise every
///          public member of \ref sentinel::w25q128 against a physical
///          W25Q128JV attached to \c sentinel::resource::cybsp_spi via
///          the bus arbiter \c sentinel::resource::cybsp_spi_bus.
///
/// \author  galudino
/// \date    2026-05-18
/// \version 1.0 - W25Q128 test implementation
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
#include "sentinel_resource.hpp"
#include "sentinel_span.hpp"
#include "sentinel_test_w25q128.hpp"
#include "sentinel_utilities.hpp"
#include "sentinel_w25q128.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace {

///
/// \brief Bus-arbitrated SPI transport for the W25Q128 test.
///
/// \details Constructed once at TU scope. Targets the SCB's SS0 line
///          (\c CYBSP_SPI_CS, P9[3]) — the only configured slave on the
///          bus today. When BME280-on-SPI lands in issue #2, that
///          test will construct its own \c cyhal_spi_bus_transport
///          with SS1.
///
sentinel::cyhal_spi_bus_transport w25q128_bus{sentinel::resource::cybsp_spi_bus,
                                              CYBSP_SPI_FLASH_CS};

///
/// \brief Yield long enough for the BLE debug ring buffer to drain.
///
inline void yield_for_debug_drain(uint32_t milliseconds) noexcept {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

///
/// \brief Convenience alias for the concrete driver instantiation.
///
using w25q128_t = sentinel::w25q128<sentinel::cyhal_spi_bus_transport>;

///
/// \brief Erase + read-back-verify a small region as blank (\c 0xFF).
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

} // namespace

// ============================================================================
// sentinel::test::w25q128::all
// ============================================================================

void sentinel::test::w25q128::all() {
    presence_check();
    yield_for_debug_drain(200);

    status_register_round_trip();
    yield_for_debug_drain(200);

    erase_program_read();
    yield_for_debug_drain(200);

    security_register_round_trip();
    yield_for_debug_drain(200);

    power_down_release();
    yield_for_debug_drain(200);

    // Never returns — 1 Hz status poll forever.
    continuous_status_poll();
}

// ============================================================================
// presence_check
// ============================================================================

void sentinel::test::w25q128::presence_check() {
    auto flash = w25q128_t(w25q128_bus);
    logi("W25Q128 presence_check: driver constructed", "");
    yield_for_debug_drain(200);

    auto raw = uint32_t{};
    raw = Cy_GPIO_Read(GPIO_PRT10, 3);
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "[%s] CS idle level: %u\n", __func__, raw, raw);

    auto jedec = flash.jedec_id();

    if (!jedec) {
        loge("presence_check FAIL: JEDEC read transport error %d",
             static_cast<int>(flash.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 presence_check FAIL: JEDEC error %d\n",
                   static_cast<int>(flash.last_error()));
        return;
    }

    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "W25Q128 presence_check: JEDEC = 0x%02X 0x%02X 0x%02X\n",
               static_cast<int>(jedec->manufacturer),
               static_cast<int>(jedec->memory_type),
               static_cast<int>(jedec->capacity));

    logi("presence_check: JEDEC = 0x%02X 0x%02X 0x%02X",
         static_cast<int>(jedec->manufacturer),
         static_cast<int>(jedec->memory_type),
         static_cast<int>(jedec->capacity));

    auto const expected = w25q128_t::jedec_id_data{
        w25q128_t::JEDEC_MANUFACTURER_ID, w25q128_t::JEDEC_MEMORY_TYPE,
        w25q128_t::JEDEC_CAPACITY};
    if (*jedec != expected) {
        loge("presence_check FAIL: JEDEC mismatch (expected 0x%02X 0x%02X "
             "0x%02X)",
             static_cast<int>(expected.manufacturer),
             static_cast<int>(expected.memory_type),
             static_cast<int>(expected.capacity));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 presence_check FAIL: JEDEC mismatch (expected "
                   "0x%02X 0x%02X 0x%02X)\n",
                   static_cast<int>(expected.manufacturer),
                   static_cast<int>(expected.memory_type),
                   static_cast<int>(expected.capacity));
        return;
    }

    // Bonus diagnostics — manufacturer+device ID and unique ID.
    if (auto mfr_dev = flash.manufacturer_device_id()) {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 presence_check: Mfr/Dev = 0x%02X 0x%02X\n",
                   static_cast<int>(mfr_dev->manufacturer),
                   static_cast<int>(mfr_dev->device));
        logi("presence_check: Mfr/Dev = 0x%02X 0x%02X",
             static_cast<int>(mfr_dev->manufacturer),
             static_cast<int>(mfr_dev->device));
    } else {
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 presence_check: Mfr/Dev read error %d\n",
                   static_cast<int>(flash.last_error()));
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

    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "W25Q128 presence_check PASS: JEDEC 0x%02X 0x%02X 0x%02X\n",
               static_cast<int>(jedec->manufacturer),
               static_cast<int>(jedec->memory_type),
               static_cast<int>(jedec->capacity));
}

// ============================================================================
// status_register_round_trip
// ============================================================================

void sentinel::test::w25q128::status_register_round_trip() {
    auto flash = w25q128_t(w25q128_bus);
    logi("W25Q128 status_register_round_trip: driver constructed", "");
    yield_for_debug_drain(200);

    auto original = flash.read_status_register_1();
    if (!original) {
        loge("status_round_trip FAIL: initial SR1 read error %d",
             static_cast<int>(flash.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 status_round_trip FAIL: initial SR1 %d\n",
                   static_cast<int>(flash.last_error()));
        return;
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
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 status_round_trip FAIL: SR1 write %d\n",
                   static_cast<int>(flash.last_error()));
        return;
    }

    auto readback = flash.read_status_register_1();
    if (!readback) {
        loge("status_round_trip FAIL: SR1 readback error %d",
             static_cast<int>(flash.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 status_round_trip FAIL: SR1 readback %d\n",
                   static_cast<int>(flash.last_error()));
        return;
    }

    // The volatile mask cannot affect read-only bits (BUSY, WEL); compare
    // only writable bits to avoid spurious mismatch.
    constexpr uint8_t writable_mask = 0xFCu; // bits 2..7 are writable
    if ((static_cast<uint8_t>(*readback) & writable_mask) !=
        (target & writable_mask)) {
        loge("status_round_trip FAIL: readback 0x%02X != target 0x%02X",
             static_cast<int>(*readback), static_cast<int>(target));
        cy_log_msg(
            CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
            "W25Q128 status_round_trip FAIL: readback 0x%02X != 0x%02X\n",
            static_cast<int>(*readback), static_cast<int>(target));
    } else {
        logi("status_round_trip PASS: SR1 round-tripped 0x%02X -> 0x%02X",
             static_cast<int>(*original), static_cast<int>(*readback));
        cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                   "W25Q128 status_round_trip PASS: SR1 0x%02X -> 0x%02X\n",
                   static_cast<int>(*original), static_cast<int>(*readback));
    }

    // Restore original (volatile write again).
    if (!flash.write_status_register_1(*original, /*volatile_only=*/true)) {
        logw("status_round_trip: restore error %d",
             static_cast<int>(flash.last_error()));
    }
}

// ============================================================================
// erase_program_read
// ============================================================================

void sentinel::test::w25q128::erase_program_read() {
    auto flash = w25q128_t(w25q128_bus);
    logi("W25Q128 erase_program_read: driver constructed", "");
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
    logi("erase_program_read: erasing sector at 0x%06X", test_address);
    if (!flash.sector_erase_4kb(test_address)) {
        loge("erase_program_read FAIL: sector_erase error %d",
             static_cast<int>(flash.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 erase_program_read FAIL: erase %d\n",
                   static_cast<int>(flash.last_error()));
        return;
    }
    yield_for_debug_drain(50);

    // 2. Verify blank.
    if (!region_is_blank(flash, test_address, verify_length)) {
        loge("erase_program_read FAIL: post-erase region not blank "
             "(last_err=%d)",
             static_cast<int>(flash.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 erase_program_read FAIL: not blank after erase\n");
        return;
    }
    logi("erase_program_read: post-erase region is blank", "");

    // 3. Program the page.
    if (!flash.page_program(
            test_address,
            sentinel::make_cspan(pattern.data(), pattern.size()))) {
        loge("erase_program_read FAIL: page_program error %d",
             static_cast<int>(flash.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 erase_program_read FAIL: program %d\n",
                   static_cast<int>(flash.last_error()));
        return;
    }
    logi("erase_program_read: programmed 256 bytes", "");

    // 4. Read back and verify.
    auto readback = std::array<uint8_t, verify_length>{};
    if (!flash.read_data(test_address, sentinel::make_span(readback.data(),
                                                           readback.size()))) {
        loge("erase_program_read FAIL: read_data error %d",
             static_cast<int>(flash.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 erase_program_read FAIL: read %d\n",
                   static_cast<int>(flash.last_error()));
        return;
    }

    for (auto i = uint32_t{0}; i < verify_length; i++) {
        if (readback[i] != pattern[i]) {
            loge("erase_program_read FAIL: byte %u readback=0x%02X "
                 "expected=0x%02X",
                 static_cast<unsigned>(i), static_cast<int>(readback[i]),
                 static_cast<int>(pattern[i]));
            cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                       "W25Q128 erase_program_read FAIL: mismatch at %u\n",
                       static_cast<unsigned>(i));
            return;
        }
    }

    logi("erase_program_read PASS: 256 bytes round-tripped at 0x%06X",
         test_address);
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "W25Q128 erase_program_read PASS: 256 B at 0x%06X\n",
               test_address);
}

// ============================================================================
// security_register_round_trip
// ============================================================================

void sentinel::test::w25q128::security_register_round_trip() {
    auto flash = w25q128_t(w25q128_bus);
    logi("W25Q128 security_register_round_trip: driver constructed", "");
    yield_for_debug_drain(200);

    constexpr uint8_t reg_index = 3; // least likely to collide with future use.

    // 1. Erase.
    if (!flash.erase_security_register(reg_index)) {
        loge("security_round_trip FAIL: erase reg %u error %d",
             static_cast<unsigned>(reg_index),
             static_cast<int>(flash.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 security_round_trip FAIL: erase %d\n",
                   static_cast<int>(flash.last_error()));
        return;
    }

    // 2. Confirm blank — read the first 16 bytes.
    auto blank_check = std::array<uint8_t, 16>{};
    if (!flash.read_security_register(
            reg_index, 0,
            sentinel::make_span(blank_check.data(), blank_check.size()))) {
        loge("security_round_trip FAIL: post-erase read error %d",
             static_cast<int>(flash.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 security_round_trip FAIL: post-erase read %d\n",
                   static_cast<int>(flash.last_error()));
        return;
    }
    for (auto i = size_t{0}; i < blank_check.size(); i++) {
        if (blank_check[i] != 0xFF) {
            loge("security_round_trip FAIL: post-erase byte %u = 0x%02X",
                 static_cast<unsigned>(i), static_cast<int>(blank_check[i]));
            cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                       "W25Q128 security_round_trip FAIL: not blank\n");
            return;
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
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 security_round_trip FAIL: program %d\n",
                   static_cast<int>(flash.last_error()));
        return;
    }

    // 4. Read back and verify.
    auto readback = std::array<uint8_t, 16>{};
    if (!flash.read_security_register(
            reg_index, 0,
            sentinel::make_span(readback.data(), readback.size()))) {
        loge("security_round_trip FAIL: readback error %d",
             static_cast<int>(flash.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 security_round_trip FAIL: readback %d\n",
                   static_cast<int>(flash.last_error()));
        return;
    }
    for (auto i = size_t{0}; i < pattern.size(); i++) {
        if (readback[i] != pattern[i]) {
            loge("security_round_trip FAIL: byte %u readback=0x%02X "
                 "expected=0x%02X",
                 static_cast<unsigned>(i), static_cast<int>(readback[i]),
                 static_cast<int>(pattern[i]));
            cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                       "W25Q128 security_round_trip FAIL: mismatch at %u\n",
                       static_cast<unsigned>(i));
            return;
        }
    }

    // 5. Erase again so the security register is left clean for next run.
    if (!flash.erase_security_register(reg_index)) {
        logw("security_round_trip: final cleanup erase error %d",
             static_cast<int>(flash.last_error()));
    }

    logi("security_round_trip PASS: security reg %u round-tripped 16 bytes",
         static_cast<unsigned>(reg_index));
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "W25Q128 security_round_trip PASS: reg %u, 16 B\n",
               static_cast<unsigned>(reg_index));
}

// ============================================================================
// power_down_release
// ============================================================================

void sentinel::test::w25q128::power_down_release() {
    auto flash = w25q128_t(w25q128_bus);
    logi("W25Q128 power_down_release: driver constructed", "");
    yield_for_debug_drain(200);

    // 1. Enter deep power-down.
    if (!flash.power_down()) {
        loge("power_down_release FAIL: power_down error %d",
             static_cast<int>(flash.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 power_down_release FAIL: power_down %d\n",
                   static_cast<int>(flash.last_error()));
        return;
    }

    // tDP (CS high to power-down) = 3 µs typical. Wait a bit longer.
    vTaskDelay(pdMS_TO_TICKS(1));

    // 2. Probe with JEDEC — chip should be unresponsive (mismatched ID).
    auto probe = flash.jedec_id();
    auto in_pd =
        !probe || probe->manufacturer != w25q128_t::JEDEC_MANUFACTURER_ID;
    if (!in_pd) {
        logw("power_down_release: chip still answering JEDEC during PD "
             "(some W25Q variants ignore commands silently rather than "
             "returning garbage — non-fatal)",
             "");
    } else {
        logi("power_down_release: confirmed unresponsive during PD", "");
    }

    // 3. Release power-down and read device ID.
    auto device_id = flash.release_power_down_device_id();
    if (!device_id) {
        loge("power_down_release FAIL: release/device_id error %d",
             static_cast<int>(flash.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 power_down_release FAIL: release %d\n",
                   static_cast<int>(flash.last_error()));
        return;
    }
    if (*device_id != w25q128_t::RELEASE_POWER_DOWN_DEVICE_ID) {
        loge("power_down_release FAIL: device_id 0x%02X != expected 0x%02X",
             static_cast<int>(*device_id),
             static_cast<int>(w25q128_t::RELEASE_POWER_DOWN_DEVICE_ID));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 power_down_release FAIL: device_id 0x%02X\n",
                   static_cast<int>(*device_id));
        return;
    }
    logi("power_down_release: release returned device_id 0x%02X",
         static_cast<int>(*device_id));

    // 4. JEDEC again to confirm full responsiveness.
    auto recheck = flash.jedec_id();
    if (!recheck || recheck->manufacturer != w25q128_t::JEDEC_MANUFACTURER_ID ||
        recheck->memory_type != w25q128_t::JEDEC_MEMORY_TYPE ||
        recheck->capacity != w25q128_t::JEDEC_CAPACITY) {
        loge("power_down_release FAIL: post-release JEDEC mismatch", "");
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "W25Q128 power_down_release FAIL: post-release JEDEC\n");
        return;
    }

    logi("power_down_release PASS: PD -> release -> JEDEC round-trip OK", "");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO, "W25Q128 power_down_release PASS\n");
}

// ============================================================================
// continuous_status_poll
// ============================================================================

[[noreturn]] void sentinel::test::w25q128::continuous_status_poll() {
    auto flash = w25q128_t(w25q128_bus);
    logi("W25Q128 continuous_status_poll: entering 1 Hz loop", "");
    cy_log_msg(CYLF_DEF, CY_LOG_INFO,
               "W25Q128 continuous_status_poll: entering 1 Hz loop\n");
    yield_for_debug_drain(200);

    while (true) {
        auto sr1 = flash.read_status_register_1();
        auto sr2 = flash.read_status_register_2();
        auto sr3 = flash.read_status_register_3();

        if (!sr1 || !sr2 || !sr3) {
            loge("continuous_status_poll: read error %d",
                 static_cast<int>(flash.last_error()));
            cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                       "W25Q128 continuous_status_poll: read error %d\n",
                       static_cast<int>(flash.last_error()));
        } else {
            auto busy =
                (*sr1 & (1u << w25q128_t::status_register_1::BUSY_BIT)) != 0;
            logi("W25Q128 SR1=0x%02X SR2=0x%02X SR3=0x%02X busy=%d",
                 static_cast<int>(*sr1), static_cast<int>(*sr2),
                 static_cast<int>(*sr3), busy ? 1 : 0);
            cy_log_msg(CYLF_DEF, CY_LOG_INFO,
                       "W25Q128 SR1=0x%02X SR2=0x%02X SR3=0x%02X busy=%d\n",
                       static_cast<int>(*sr1), static_cast<int>(*sr2),
                       static_cast<int>(*sr3), busy ? 1 : 0);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================================
// task_create
// ============================================================================

BaseType_t sentinel::test::w25q128::task_create() {
    constexpr auto stack_words = configMINIMAL_STACK_SIZE * 4;
    constexpr auto priority =
        static_cast<UBaseType_t>(configMAX_PRIORITIES - 3);

    return xTaskCreate([](void *) -> void { sentinel::test::w25q128::all(); },
                       "W25Q128 Test Task", stack_words, nullptr, priority,
                       nullptr);
}
