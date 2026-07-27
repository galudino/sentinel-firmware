///
/// \file    sentinel_device_context.hpp
/// \brief   Shared application device context — drivers + flash stores as one
///          application-scoped singleton (decisions #13 + #17, issue #38)
///
/// \details Promotes the sensor / storage drivers and the flash-backed record
///          stores from task-local instances to a single application-scoped
///          aggregate, borrowed by reference by every consumer (the boot
///          orchestrator's POST run, \ref sentinel::task::rtc_service,
///          \ref sentinel::task::bme280_service, the snapshot persistence task
///          #38, the live snapshot stream #46, and the ~6 GATT services of #6).
///          This completes the resource-layer shared-device pattern the SCB bus
///          handles and \c flash_device_mutex (decision #4) already started:
///          one \c bme280 (so the factory-calibration read in its constructor
///          happens exactly once, not once per consumer), one \c ds3231, one
///          \c w25q128, and the two flash regions that sit on it.
///
///          === Why a post-scheduler Meyers singleton, not inline globals
///          (decision #17, amends #13's literal wording) ===
///
///          Decision #13 sketched these as file-scope \c inline objects "the
///          same way the cyhal SCB bus handles live there." That cannot work
///          literally: the \ref sentinel::bme280 constructor reads factory
///          calibration over I²C, and \e all bus I/O is serviced by the bus-
///          arbiter tasks, which only pump after \c vTaskStartScheduler(). A
///          file-scope object is constructed before \c main() runs the
///          scheduler, so its calibration read would dead-lock on an arbiter
///          that never runs. The context is therefore a function-local
///          \c static (\ref sentinel::resource::context()) first touched from
///          the boot-orchestrator
///          task — i.e. post-scheduler, on the production arbiter path — exactly
///          like the testbench fixtures (#48). End state is identical to #13's
///          intent: one instance owned by \c sentinel::resource, borrowed by
///          reference; only the construction \e moment differs.
///
/// \author  galudino
/// \date    2026-06-30
/// \version 1.0 - Shared device context (issue #38)
///

#ifndef SENTINEL_DEVICE_CONTEXT_HPP
#define SENTINEL_DEVICE_CONTEXT_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "bme280_defs.h" ///< BME280_I2C_ADDR_PRIM
#include "cycfg_pins.h"  ///< CYBSP_SPI_FLASH_CS
#include "semphr.h"
}
#pragma GCC diagnostic pop

#include "sentinel_bme280.hpp"
#include "sentinel_cyhal_i2c_bus_transport.hpp"
#include "sentinel_cyhal_spi_bus_transport.hpp"
#include "sentinel_device_snapshot.hpp"
#include "sentinel_ds3231.hpp"
#include "sentinel_record_store.hpp"
#include "sentinel_resource.hpp"
#include "sentinel_system_event.hpp"
#include "sentinel_system_event_log.hpp"
#include "sentinel_task_rtc_service.hpp"
#include "sentinel_w25q128.hpp"

#include <cstdint>

namespace sentinel::resource {

// ===========================================================================
// Flash map (single authority for the on-flash region layout)
// ===========================================================================
//
// The W25Q128 is 16 MiB. The two persistent logs sit in fixed, non-overlapping
// regions low in flash; the testbench scratch regions (0xF00000 record_store,
// 0xFFF000 w25q128) live high and never collide with these. The event-log
// region offset/size are owned by the System Event Log header (#34, decision
// #11); the snapshot region is finalized here alongside it (#38, decision #13).
//
//   [0x100000 .. 0x180000)  System Event Log   512 KiB  (#34)
//   [0x180000 .. 0x280000)  Device Snapshots     1 MiB  (#38)
//

/// \brief Snapshot-history region base offset — immediately after the event log.
inline constexpr uint32_t kSnapshotRegionOffsetBytes =
    sentinel::diagnostics::kEventLogRegionOffsetBytes +
    sentinel::diagnostics::kEventLogRegionSizeBytes; // 0x180000

/// \brief Snapshot-history region size — 1 MiB (decision #14: ~14 days at the
///        5-minute shipping cadence; ~4096 80-byte snapshots).
inline constexpr uint32_t kSnapshotRegionSizeBytes = 0x100000u; // 1 MiB

static_assert(kSnapshotRegionOffsetBytes >=
                  sentinel::diagnostics::kEventLogRegionOffsetBytes +
                      sentinel::diagnostics::kEventLogRegionSizeBytes,
              "snapshot region must not overlap the event-log region");

// ===========================================================================
// Concrete instantiations (production cyhal bus transports)
// ===========================================================================

/// \brief BME280 over the bus-arbitrated I²C transport.
using bme280_t = sentinel::bme280_i2c<sentinel::cyhal_i2c_bus_transport>;

/// \brief DS3231 over the bus-arbitrated I²C transport.
using ds3231_t = sentinel::ds3231<sentinel::cyhal_i2c_bus_transport>;

/// \brief W25Q128 over the bus-arbitrated SPI transport.
using w25q128_t = sentinel::w25q128<sentinel::cyhal_spi_bus_transport>;

/// \brief Flash-backed store of 36-byte System Event Log records (#34).
using event_store_t =
    sentinel::record_store<sentinel::diagnostics::system_event_record,
                           sentinel::cyhal_spi_bus_transport>;

/// \brief Flash-backed store of 80-byte device snapshots (#38, lane 1).
using snapshot_store_t =
    sentinel::record_store<sentinel::telemetry::device_snapshot,
                           sentinel::cyhal_spi_bus_transport>;

/// \brief The application's System Event Log, bound to \ref event_store_t.
using event_log_t = sentinel::diagnostics::system_event_log<event_store_t>;

///
/// \brief Clock callback for the System Event Log: Unix seconds, or 0.
///
/// \details Thin wrapper over \ref sentinel::task::rtc_service so the event log
///          stays decoupled from the RTC driver (decision #11). Matches
///          \ref event_log_t::now_unix_fn.
///
/// \return Current Unix time in seconds, or 0 if the RTC has not yet
///         reported one.
///
inline uint32_t now_unix_seconds() noexcept {
    return sentinel::task::rtc_service::instance().last_unix_time();
}

///
/// \brief Aggregate of the shared drivers + flash stores, borrowed by reference.
///
/// \details One instance, reached via \ref context(). Public members so the boot
///          orchestrator can wire them directly (e.g.
///          \c post::run(ctx.bme, ctx.rtc, ctx.flash, ctx.event_store, …)).
///          Constructor order is significant: transports precede the drivers
///          that bind them, and the flash driver precedes the stores layered on
///          it. The \c bme constructor performs its calibration read here, so
///          the first \ref context() call must be post-scheduler.
///
struct device_context {
    // ---- Bus transports (one per device; each carries its own target). ----
    sentinel::cyhal_i2c_bus_transport bme_bus{sentinel::resource::cybsp_i2c_bus,
                                              BME280_I2C_ADDR_PRIM}; ///< BME280's I2C transport.
    sentinel::cyhal_i2c_bus_transport rtc_bus{
        sentinel::resource::cybsp_i2c_bus,
        static_cast<uint16_t>(ds3231_t::slave_address::primary)}; ///< DS3231's I2C transport.
    sentinel::cyhal_spi_bus_transport flash_bus{
        sentinel::resource::cybsp_spi_bus, CYBSP_SPI_FLASH_CS}; ///< W25Q128's SPI transport.

    // ---- Drivers. ----
    bme280_t  bme{bme_bus, BME280_I2C_ADDR_PRIM}; ///< Temperature/humidity/pressure sensor.
    ds3231_t  rtc{rtc_bus};                       ///< Real-time clock.
    w25q128_t flash{flash_bus, sentinel::resource::flash_device_mutex}; ///< SPI NOR flash.

    // ---- Flash-backed record stores. ----
    event_store_t event_store{flash, sentinel::diagnostics::kEventLogRegionOffsetBytes,
                              sentinel::diagnostics::kEventLogRegionSizeBytes}; ///< System Event Log's store.
    snapshot_store_t snapshot_store{flash, kSnapshotRegionOffsetBytes,
                                    kSnapshotRegionSizeBytes}; ///< Snapshot history's store.

    ///
    /// \brief First failed POST subsystem id (0 = all passed), cached by the
    ///        orchestrator so \c populate_snapshot can report it (#36 field).
    ///
    uint8_t post_last_status{0};

    /// \brief The application System Event Log singleton (bound by
    ///        \ref initialize_stores).
    /// \return Reference to the singleton \ref event_log_t instance.
    static event_log_t &event_log() noexcept { return event_log_t::instance(); }

    /// \brief Valid records currently in the event log region.
    /// \return Count of valid records in \ref event_store.
    uint32_t event_log_record_count() const noexcept {
        return event_store.count();
    }

    /// \brief Valid records currently in the snapshot history region.
    /// \return Count of valid records in \ref snapshot_store.
    uint32_t snapshot_record_count() const noexcept {
        return snapshot_store.count();
    }
};

///
/// \brief The single application device context.
///
/// \details Constructed on first call (the \c bme calibration read happens
///          then), so the first call MUST come from a task running after the
///          scheduler has started — the boot orchestrator (app) or the test
///          orchestrator (testbench). Borrowed by reference everywhere else.
///
/// \return Reference to the single application \ref device_context.
///
inline device_context &context() noexcept {
    static device_context ctx;
    return ctx;
}

///
/// \brief \c true once \ref initialize_stores has scanned the flash regions and
///        bound the event log.
///
/// \details Lets \c populate_snapshot (#36) read the store counts only when they
///          are meaningful, without forcing a (post-scheduler-only) context
///          construction on a caller that runs before the orchestrator.
///
inline bool g_context_ready = false;

/// \brief Has the context been built and its stores initialized?
/// \return \c true once \ref initialize_stores has completed successfully.
inline bool context_ready() noexcept { return g_context_ready; }

///
/// \brief Recover both flash stores from their on-flash state and bind the
///        event log to its store + clock. Idempotent.
///
/// \details Called once by the boot orchestrator after \ref context() is first
///          constructed and before POST / service tasks run. POST's read-only
///          record-store probe re-scans the event store harmlessly. Sets
///          \ref context_ready so \c populate_snapshot may read the counts.
///
/// \return \c true if both stores initialized and the log bound; \c false on a
///         flash-scan failure.
///
inline bool initialize_stores() noexcept {
    auto &ctx = context();
    const auto event_ok    = ctx.event_store.initialize();
    const auto snapshot_ok = ctx.snapshot_store.initialize();
    const auto log_ok =
        event_log_t::instance().initialize(ctx.event_store, &now_unix_seconds);
    g_context_ready = event_ok && snapshot_ok && log_ok;
    return g_context_ready;
}

} // namespace sentinel::resource

#endif /* SENTINEL_DEVICE_CONTEXT_HPP */
