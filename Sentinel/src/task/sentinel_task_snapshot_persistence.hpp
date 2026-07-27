///
/// \file    sentinel_task_snapshot_persistence.hpp
/// \brief   Periodic device_snapshot flash persistence task (lane 1, #38)
///
/// \details Declares the always-on snapshot persistence task: a low-priority
///          FreeRTOS task that appends a \ref sentinel::telemetry::device_snapshot
///          to the flash-backed snapshot history store at a configurable cadence
///          (shipping default ~5 min / 300 s; decision #14), connected or not,
///          for the device's whole operational life. This is \b lane \b 1 of the
///          two-lane snapshot model (decision #14): the slow, always-on,
///          flash-persisted history — distinct from the fast on-demand live BLE
///          stream (#46, lane 2). Both call the same cache-backed
///          \c populate_snapshot() primitive (#36), so neither issues fresh bus
///          I/O on the populate path.
///
///          Architecturally the snapshot twin of the System Event Log (#34):
///          same \ref sentinel::record_store backing, same wrap-and-overwrite-
///          oldest policy, same BLE-retrievable paged-read shape — only the
///          record is larger (80 B vs 36 B) and the cadence slower. The
///          \ref sentinel::diagnostics::system_event::snapshot_persisted
///          heartbeat is recorded to the event log once per
///          \ref sentinel::task::snapshot_persistence_task::HEARTBEAT_EVERY_N
///          captures so the log carries a "still alive" marker without
///          flooding.
///
///          OO/class style (decision #16): cadence, store binding, and handle
///          live in private members; the loop runs as a private
///          \ref sentinel::task::snapshot_persistence_task::run reached via a
///          static trampoline. Use the
///          \ref sentinel::task::snapshot_persistence_task::instance singleton.
///
/// \author  galudino
/// \date    2026-06-30
/// \version 1.0 - Initial snapshot persistence task
///

#ifndef SENTINEL_TASK_SNAPSHOT_PERSISTENCE_HPP
#define SENTINEL_TASK_SNAPSHOT_PERSISTENCE_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

#include "sentinel_device_context.hpp"

#include <cstdint>

namespace sentinel::telemetry {
struct device_snapshot;
} // namespace sentinel::telemetry

namespace sentinel::task {

///
/// \brief Single-owner FreeRTOS task that persists a \c device_snapshot to flash
///        every \ref period_seconds (lane 1, #38).
///
/// \note    This class is non-copyable and non-movable.
///
class snapshot_persistence_task {
public:
    ///
    /// \brief Shipping cadence — ~5 min (decision #14); long history within the
    ///        flash wear budget. Faster (down to 1 s) is supported for lab use.
    ///
    static constexpr uint32_t DEFAULT_PERIOD_SECONDS = 300;

    ///
    /// \brief Record a \c snapshot_persisted heartbeat every N captures, giving
    ///        the System Event Log a low-rate "still alive" marker.
    ///
    static constexpr uint32_t HEARTBEAT_EVERY_N = 60;

    ///
    /// \brief The single snapshot-persistence-task instance.
    ///
    /// \return Reference to the singleton \ref snapshot_persistence_task instance.
    ///
    static snapshot_persistence_task &instance() noexcept;

    /// Non-copyable, non-movable: the task entry point captures \c this.
    snapshot_persistence_task(const snapshot_persistence_task &) = delete;
    snapshot_persistence_task &
    operator=(const snapshot_persistence_task &) = delete;
    snapshot_persistence_task(snapshot_persistence_task &&) = delete;
    snapshot_persistence_task &operator=(snapshot_persistence_task &&) = delete;

    ///
    /// \brief Create and start the persistence task.
    ///
    /// \param priority       FreeRTOS priority. Default \c configMAX_PRIORITIES-4
    ///                       — snapshot timing is not latency-critical, leaving
    ///                       headroom for the sensor sample task and BLE stack.
    /// \param stack_words    Stack depth in words.
    /// \param period_seconds Initial cadence in seconds.
    /// \return \c pdPASS on success, otherwise the \c xTaskCreate failure code.
    ///
    BaseType_t task_create(
        UBaseType_t priority = static_cast<UBaseType_t>(configMAX_PRIORITIES - 4),
        uint16_t stack_words = static_cast<uint16_t>(configMINIMAL_STACK_SIZE * 6),
        uint32_t period_seconds = DEFAULT_PERIOD_SECONDS) noexcept;

    ///
    /// \brief Set the capture cadence at runtime.
    /// \param period_seconds New cadence in seconds; floored to a 1 s minimum.
    /// \return \c true (always; the value is floored to a 1 s minimum).
    ///
    bool set_period_seconds(uint32_t period_seconds) noexcept;

    /// \brief Current capture cadence in seconds.
    /// \return Current \ref set_period_seconds value, in seconds.
    uint32_t period_seconds() const noexcept;

    ///
    /// \brief Capture one snapshot now and append it to the store.
    ///
    /// \details Useful for fault-handler captures and the testbench. Records a
    ///          \c snapshot_persisted heartbeat every \ref HEARTBEAT_EVERY_N
    ///          successful captures (only on the production / context-store path).
    ///
    /// \return \c true if the snapshot was appended; \c false on a flash error.
    ///
    bool capture_now() noexcept;

    /// \brief Valid snapshots currently stored.
    /// \return Count of valid snapshots in the backing store.
    uint32_t count() const noexcept;

    /// \brief Read the snapshot at an absolute \p index (called from #6 GATT).
    /// \param index Absolute record index to read.
    /// \param out   Destination snapshot; written only on success.
    /// \return \c true if \p index held a valid record; \c false otherwise.
    bool read(uint32_t index, telemetry::device_snapshot *out) const noexcept;

    /// \brief Read \p n consecutive snapshots from absolute \p start.
    /// \param start Absolute starting record index.
    /// \param n     Number of consecutive records to read.
    /// \param out   Destination array of at least \p n snapshots.
    /// \return \c true if every requested record was valid and read;
    ///         \c false otherwise.
    bool read_range(uint32_t start, uint32_t n,
                    telemetry::device_snapshot *out) const noexcept;

    /// \brief Erase the entire snapshot history.
    /// \return \c true on success; \c false on a flash error.
    bool erase_all() noexcept;

    ///
    /// \brief Override the backing store (testability hook).
    ///
    /// \param store Store to persist into; \c nullptr restores the default
    ///              shared-context store (\c resource::context().snapshot_store).
    ///              The testbench binds a small scratch store so \c wrap_around
    ///              is reachable without filling the 1 MiB production region.
    ///
    void bind_store(resource::snapshot_store_t *store) noexcept;

private:
    snapshot_persistence_task() = default;

    /// \brief Static FreeRTOS task entry point; forwards to \ref run.
    /// \param task_parameter Unused (\c this is captured via \ref instance).
    static void task_trampoline(void *task_parameter);

    /// \brief Capture loop: \c populate → \c append → delay, forever.
    void run();

    /// \brief Resolve the effective store (bound override, else context store).
    /// \return Reference to the store bound via \ref bind_store, or the
    ///         shared \c resource::context().snapshot_store if none is bound.
    resource::snapshot_store_t &store() const noexcept;

    resource::snapshot_store_t *m_store{nullptr};  ///< Bound override (tests).
    volatile uint32_t m_period_seconds{DEFAULT_PERIOD_SECONDS}; ///< Cadence (s).
    uint32_t          m_capture_count{0};          ///< Successful captures (heartbeat).
    TaskHandle_t      m_handle{nullptr};           ///< FreeRTOS task handle.
};

} // namespace sentinel::task

#endif /* SENTINEL_TASK_SNAPSHOT_PERSISTENCE_HPP */
