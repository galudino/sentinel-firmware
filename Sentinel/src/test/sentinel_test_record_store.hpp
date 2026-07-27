///
/// \file    sentinel_test_record_store.hpp
/// \brief   Flash-backed circular record store test suite (run-to-completion)
///
/// \details Declares the testbench suite for the \ref sentinel::record_store
///          storage primitive (firmware #33). Tests exercise the store against
///          a physical W25Q128JV attached to the board's SPI bus
///          (\c sentinel::resource::cybsp_spi_bus, device CS on
///          \c CYBSP_SPI_FLASH_CS), using a dedicated scratch region of the
///          flash that does not collide with the
///          \c sentinel::test::w25q128 erase/program scratch sector
///          (\c 0xFFF000).
///
///          Test coverage maps 1:1 onto the issue's acceptance criteria:
///          - presence_check        — fresh store reports empty
///          - append_round_trip     — append one record, read it back
///          - many_append           — append 100 records, read in order
///          - wrap_around           — fill to capacity + 1, oldest overwritten
///          - power_loss_simulation — partial record skipped by recovery scan
///          - survive_reset         — fresh store re-derives head/tail
///
///          Run-to-completion (#48): the suite runs synchronously and returns
///          a \ref sentinel::test::tally. It no longer self-schedules as a
///          FreeRTOS task — the serial test orchestrator calls
///          \ref sentinel::test::record_store::run_all directly. Internally it
///          uses a TU-local fixture that owns the
///          bus-arbitrated SPI transport, so there is no file-static bus global.
///
/// \author  galudino
/// \date    2026-06-28
/// \version 2.0 - Run-to-completion suite (#48)
///

#ifndef SENTINEL_TEST_RECORD_STORE_HPP
#define SENTINEL_TEST_RECORD_STORE_HPP

#include "sentinel_test_result.hpp"

namespace sentinel::test::record_store {

///
/// \brief Run the full record_store test suite to completion.
///
/// \details Executes each test in sequence over a fixture-owned SPI transport
///          and returns the pass/fail \ref sentinel::test::tally. Intended to
///          be called by the testbench serial orchestrator (#48).
///
/// \return The suite's pass/fail tally.
///
tally run_all() noexcept;

} // namespace sentinel::test::record_store

#endif /* SENTINEL_TEST_RECORD_STORE_HPP */
