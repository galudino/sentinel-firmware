///
/// \file    sentinel_test_record_store.hpp
/// \brief   Flash-backed circular record store test declarations
///
/// \details This header declares the testbench-side tests for the
///          \ref sentinel::record_store storage primitive (firmware #33).
///          Tests exercise the store against a physical W25Q128JV attached
///          to the board's SPI bus (\c sentinel::resource::cybsp_spi_bus,
///          device CS on \c CYBSP_SPI_FLASH_CS), using a dedicated scratch
///          region of the flash that does not collide with the
///          \ref sentinel::test::w25q128 erase/program scratch sector
///          (\c 0xFFF000).
///
///          Test coverage maps 1:1 onto the issue's acceptance criteria:
///          - \ref presence_check        — initialize() on a freshly-erased
///            region succeeds and count() == 0
///          - \ref append_round_trip     — append one record, read it back
///          - \ref many_append           — append 100 records, read in order
///          - \ref wrap_around           — fill to capacity + 1, tail
///            advances, oldest overwritten, newest readable
///          - \ref power_loss_simulation — a partially-written record is
///            skipped by the recovery scan
///          - \ref survive_reset         — a fresh store re-derives head/tail
///            from on-flash state (emulated warm boot)
///
///          Per project convention every PASS / FAIL line goes to both the
///          BLE debug stream (\c logi / \c loge) and the retarget-IO UART
///          (\c cy_log_msg) so the result survives ring-buffer overflow and
///          a missing BLE central.
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - record_store test declarations
///

#ifndef SENTINEL_TEST_RECORD_STORE_HPP
#define SENTINEL_TEST_RECORD_STORE_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

namespace sentinel::test::record_store {

///
/// \brief Run the full record_store test suite.
///
void all();

///
/// \brief Erase the scratch region, initialize a fresh store, confirm it
///        reports empty (count == 0, head == tail == 0).
///
void presence_check();

///
/// \brief Append a single record and read it back byte-for-byte.
///
void append_round_trip();

///
/// \brief Append 100 records of varying contents and read each back in order.
///
void many_append();

///
/// \brief Fill the store to capacity, append one more, and confirm the tail
///        advanced, the oldest record was overwritten, and the newest record
///        is still readable. Also re-scans from flash and confirms head/tail
///        recovery matches the in-RAM state.
///
void wrap_around();

///
/// \brief Write a record's payload without committing its status byte
///        (emulating a power loss mid-append), then re-scan and confirm the
///        partial record is skipped and the count is unchanged.
///
void power_loss_simulation();

///
/// \brief Append N records, construct a fresh store over the same region,
///        initialize() (emulating a warm boot), and confirm count and reads
///        survive.
///
void survive_reset();

///
/// \brief Create the FreeRTOS task that runs \ref all.
///
BaseType_t task_create();

} // namespace sentinel::test::record_store

#endif /* SENTINEL_TEST_RECORD_STORE_HPP */
