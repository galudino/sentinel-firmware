///
/// \file    sentinel_test_w25q128.hpp
/// \brief   W25Q128 flash driver test declarations
///
/// \details This header declares the testbench-side smoke tests for the
///          \ref sentinel::w25q128 driver. Tests exercise the full
///          public API of the driver against a physical W25Q128JV
///          attached to the board's SPI bus
///          (\c sentinel::resource::cybsp_spi via
///          \c sentinel::resource::cybsp_spi_bus, with the device's
///          chip-select line on \c CYBSP_SPI_CS / SS0 / P9[3]).
///
///          Test coverage:
///          - Presence check (JEDEC ID matches 0xEF / 0x40 / 0x18,
///            manufacturer+device ID and unique ID logged)
///          - Status Register 1 round-trip (volatile write to avoid
///            persistently modifying block-protect state on failure)
///          - Erase + program + verify on the last sector of flash
///            (\c 0xFFF000, well above any plausible application data)
///          - Security Register 3 erase / program / read round-trip
///          - Deep power-down + release sequence
///          - Continuous status / BUSY poll at ~1 Hz
///
///          Per the project convention, every PASS / FAIL line goes to
///          both the BLE debug stream (\c logi / \c loge) and the
///          retarget-IO UART (\c cy_log_msg) so the result survives ring-
///          buffer overflow and missing BLE central.
///
/// \author  galudino
/// \date    2026-05-18
/// \version 1.0 - W25Q128 test declarations
///

#ifndef SENTINEL_TEST_W25Q128_HPP
#define SENTINEL_TEST_W25Q128_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

namespace sentinel::test::w25q128 {

///
/// \brief Run the full W25Q128 test suite.
///
void all();

///
/// \brief Verify the chip is reachable and identifies as W25Q128JV.
///
void presence_check();

///
/// \brief Round-trip SR1 with the volatile-write opcode to avoid
///        persistently changing block-protect state on failure.
///
void status_register_round_trip();

///
/// \brief Erase the last sector (\c 0xFFF000), verify blank, program a
///        known pattern, read back and verify byte-for-byte.
///
void erase_program_read();

///
/// \brief Round-trip Security Register 3: erase, verify blank, program
///        a known pattern at offset 0, read back, verify, then erase
///        again to leave it clean.
///
void security_register_round_trip();

///
/// \brief Power-down + release sequence: deep-power-down, attempt JEDEC
///        (should fail to match), release via \c 0xAB and confirm
///        device ID is 0x17, then re-issue JEDEC and confirm full
///        responsiveness.
///
void power_down_release();

///
/// \brief Continuous status / BUSY poll at ~1 Hz, logged via both
///        \c logi and \c cy_log_msg.
///
[[noreturn]] void continuous_status_poll();

///
/// \brief Create the FreeRTOS task that runs \ref all.
///
BaseType_t task_create();

} // namespace sentinel::test::w25q128

#endif /* SENTINEL_TEST_W25Q128_HPP */
