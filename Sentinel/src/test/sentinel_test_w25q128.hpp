///
/// \file    sentinel_test_w25q128.hpp
/// \brief   W25Q128 flash driver test suite (run-to-completion)
///
/// \details Declares the testbench smoke-test suite for the
///          \ref sentinel::w25q128 driver. The suite exercises the driver's
///          public API against a physical W25Q128JV attached to the board's
///          SPI bus (\c sentinel::resource::cybsp_spi_bus, device CS on
///          \c CYBSP_SPI_FLASH_CS).
///
///          Test coverage:
///          - Presence check (JEDEC ID matches known-good list)
///          - Status Register 1 round-trip (volatile write)
///          - Erase + program + verify on the last sector (\c 0xFFF000)
///          - Security Register 3 erase / program / read round-trip
///          - Deep power-down + release sequence
///
///          Run-to-completion (#48): the suite runs synchronously and returns
///          a \ref sentinel::test::tally. It no longer self-schedules as a
///          FreeRTOS task and no longer owns a continuous ~1 Hz status poll —
///          the serial test orchestrator calls
///          \ref sentinel::test::w25q128::run_all directly.
///
///          Internally the suite uses a TU-local fixture that owns the
///          bus-arbitrated SPI transport (the canonical "fixture holds the
///          shared resource" shape), so there is no file-static bus global.
///
///          Every PASS / FAIL line goes through \c logi / \c loge, which
///          the logging facade (#50) writes to both the retarget-IO UART and
///          the BLE debug stream.
///
/// \author  galudino
/// \date    2026-05-18
/// \version 2.0 - Run-to-completion suite (#48)
///

#ifndef SENTINEL_TEST_W25Q128_HPP
#define SENTINEL_TEST_W25Q128_HPP

#include "sentinel_test_result.hpp"

namespace sentinel::test::w25q128 {

///
/// \brief Run the full W25Q128 test suite to completion.
///
/// \details Executes each test in sequence — presence check, status-register
///          round-trip, erase/program/read, security-register round-trip,
///          power-down/release — over a fixture-owned SPI transport, and
///          returns the pass/fail \ref sentinel::test::tally. Intended to be
///          called by the testbench serial orchestrator (#48).
///
/// \return The suite's pass/fail tally.
///
tally run_all() noexcept;

} // namespace sentinel::test::w25q128

#endif /* SENTINEL_TEST_W25Q128_HPP */
