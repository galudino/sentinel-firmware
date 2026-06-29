///
/// \file    sentinel_task_snapshot_stream.hpp
/// \brief   Live device_snapshot BLE stream service task (lane 2, #46)
///
/// \details Declares the live snapshot stream task: a dedicated FreeRTOS task
///          that streams the live \ref sentinel::telemetry::device_snapshot (#36)
///          to a connected BLE central at a configurable cadence (default
///          ~100 ms), \b only while a capture session is active. This is
///          \b lane \b 2 of the two-lane snapshot model (handoff decision #14):
///          the fast, on-demand, connection-gated live stream — distinct from
///          the always-on slow flash persistence (#38, lane 1).
///
///          Default state is \b idle: the task blocks with zero CPU until the
///          \c SnapshotStream enable characteristic (#6) calls \ref start. It
///          then loops \c populate_snapshot() → notify sink at \ref period_ms
///          until \ref stop, the central disconnects, or notifications are
///          unsubscribed — then it returns to idle.
///
///          \b Producer/GATT \b split (decision #14, option a). This task only
///          \e produces the snapshot and hands it to a notify sink; the actual
///          \c wiced_bt_gatt notification lives in #6's GATT handler, attached
///          here via \ref set_notify_sink. Keeping the producer loop here leaves
///          #6 as pure GATT wiring rather than a producer loop embedded in the
///          GATT callbacks.
///
///          OO/class style, mirroring \ref sentinel::task::bme280_service and
///          the bus arbiters (handoff decision #16): the task's state (enable
///          flag, cadence, sinks, handle) lives in private members rather than
///          \c .cpp file-static globals, and the loop runs as a private
///          \ref run reached via a static trampoline.
///
/// \author  galudino
/// \date    2026-06-29
/// \version 1.0 - Initial live snapshot stream task
///

#ifndef SENTINEL_TASK_SNAPSHOT_STREAM_HPP
#define SENTINEL_TASK_SNAPSHOT_STREAM_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

#include <cstdint>

namespace sentinel::telemetry {
struct device_snapshot;
} // namespace sentinel::telemetry

namespace sentinel::task {

///
/// \brief Single-owner FreeRTOS task that streams the live \c device_snapshot
///        (#36) to a connected BLE central while a capture session is active.
///
/// \details Normally idle (blocked, zero CPU). The \c SnapshotStream enable
///          characteristic (#6) wakes it via \ref start; while enabled \b and
///          a central is connected it loops \c populate_snapshot() → notify sink
///          at \ref period_ms, then auto-returns to idle on \ref stop or
///          disconnect. Use the \ref instance singleton.
///
/// \note    This class is non-copyable and non-movable.
///
class snapshot_stream_task {
public:
    ///
    /// \brief BLE notify sink — how a produced snapshot is sent on the wire.
    ///
    /// \details The GATT layer (#6) attaches this via \ref set_notify_sink at
    ///          GATT-DB registration; until then the task runs against a
    ///          testbench sink that counts invocations, so this task is fully
    ///          testable before #6 exists.
    ///
    using notify_fn = void (*)(const telemetry::device_snapshot &) noexcept;

    ///
    /// \brief Connection-state predicate — \c true while a central is connected.
    ///
    /// \details Defaults to the live BLE context query
    ///          (\c ble_context_object.connected()). Overridable via
    ///          \ref set_connected_predicate so the off-bench testbench can
    ///          simulate a mid-stream disconnect (AC \c disconnect_autostop).
    ///
    using connected_fn = bool (*)() noexcept;

    ///
    /// \brief Default ~100 ms streaming cadence.
    ///
    static constexpr uint32_t DEFAULT_PERIOD_MS = 100;

    ///
    /// \brief The single snapshot-stream-task instance.
    ///
    static snapshot_stream_task &instance() noexcept;

    /// Non-copyable, non-movable: the task entry point captures \c this.
    snapshot_stream_task(const snapshot_stream_task &) = delete;
    snapshot_stream_task &operator=(const snapshot_stream_task &) = delete;
    snapshot_stream_task(snapshot_stream_task &&) = delete;
    snapshot_stream_task &operator=(snapshot_stream_task &&) = delete;

    ///
    /// \brief Create and start the snapshot stream task (idle until \ref start).
    ///
    /// \param priority    FreeRTOS task priority.
    /// \param stack_words Stack depth in words.
    /// \param period_ms   Initial streaming cadence in milliseconds.
    /// \return \c pdPASS on success, otherwise the \c xTaskCreate failure code.
    ///
    BaseType_t task_create(
        UBaseType_t priority = static_cast<UBaseType_t>(configMAX_PRIORITIES - 3),
        uint16_t stack_words = static_cast<uint16_t>(configMINIMAL_STACK_SIZE * 6),
        uint32_t period_ms = DEFAULT_PERIOD_MS) noexcept;

    ///
    /// \brief Begin streaming (idempotent).
    ///
    /// \details Called by the \c SnapshotStream enable-char GATT handler (#6)
    ///          on \c enable == 1. Marks the session active and wakes the idle
    ///          task; a second call while already streaming is a no-op.
    ///
    void start() noexcept;

    ///
    /// \brief Return to idle (idempotent).
    ///
    /// \details Called on \c enable == 0 (or by #6 on unsubscribe). The task
    ///          finishes its current cadence delay, then blocks until the next
    ///          \ref start. A call while already idle is a no-op.
    ///
    void stop() noexcept;

    ///
    /// \brief \c true while a capture session is active.
    ///
    bool streaming() const noexcept;

    ///
    /// \brief Set the streaming cadence at runtime.
    ///
    /// \param period_ms New period in milliseconds; floored to a small minimum
    ///                  so a runaway value cannot saturate notify throughput.
    ///
    void set_period_ms(uint32_t period_ms) noexcept;

    ///
    /// \brief Current streaming cadence in milliseconds.
    ///
    uint32_t period_ms() const noexcept;

    ///
    /// \brief Attach the BLE notify sink (set by #6 at GATT-DB registration).
    ///
    /// \param sink Function the task calls with each produced snapshot. Passing
    ///             \c nullptr detaches the sink (the task still runs but emits
    ///             nothing).
    ///
    void set_notify_sink(notify_fn sink) noexcept;

    ///
    /// \brief Override the connection-state predicate (testability hook).
    ///
    /// \param predicate Returns \c true while a central is connected. Passing
    ///                  \c nullptr restores the default live BLE-context query.
    ///
    void set_connected_predicate(connected_fn predicate) noexcept;

private:
    snapshot_stream_task() = default;

    static void task_trampoline(void *task_parameter);

    ///
    /// \brief Idle ↔ stream loop: block while idle, then notify at the cadence
    ///        while enabled and connected; auto-stop on disconnect.
    ///
    void run();

    /// \brief Resolve the effective connection state (predicate or live BLE).
    bool central_connected() const noexcept;

    volatile bool     m_streaming{false};  ///< Session active? (requested state)
    volatile uint32_t m_period_ms{DEFAULT_PERIOD_MS}; ///< Streaming cadence (ms).
    notify_fn         m_notify_sink{nullptr};         ///< BLE notify sink (#6).
    connected_fn      m_connected{nullptr};           ///< Connection predicate.
    TaskHandle_t      m_handle{nullptr};              ///< FreeRTOS task handle.
};

} // namespace sentinel::task

#endif /* SENTINEL_TASK_SNAPSHOT_STREAM_HPP */
