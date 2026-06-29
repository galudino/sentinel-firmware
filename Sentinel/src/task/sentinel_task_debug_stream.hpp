///
/// \file       sentinel_task_debug_stream.hpp
/// \brief      BLE Debug Output Stream - Header
///
/// Provides a ring buffer-based debug output system that transmits log messages
/// over BLE notifications to a connected iOS client application.
///
/// Features:
/// - Non-blocking printf-style API
/// - MTU-aware packetization
/// - FreeRTOS task-safe ring buffer
/// - Best-effort (drop-on-full) semantics
///
/// \author     galudino
/// \date       2026-05-15
///

#ifndef SENTINEL_TASK_DEBUG_STREAM_HPP
#define SENTINEL_TASK_DEBUG_STREAM_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace sentinel::task {

///
/// \brief Single-owner FreeRTOS task that drains the debug ring buffer and
///        transmits log messages over BLE notifications.
///
/// \details OO/class style, mirroring \ref sentinel::task::spi_bus: the task
///          handle lives in a private member rather than a \c .cpp file-static
///          global, and the loop runs as a private \ref run reached via a
///          static trampoline. Use the \ref instance singleton.
///
/// \note    This class is non-copyable and non-movable.
///
class debug_stream {
public:
    ///
    /// \brief The single debug-stream instance.
    ///
    static debug_stream &instance() noexcept;

    /// Non-copyable, non-movable: the task entry point captures \c this.
    debug_stream(const debug_stream &) = delete;
    debug_stream &operator=(const debug_stream &) = delete;
    debug_stream(debug_stream &&) = delete;
    debug_stream &operator=(debug_stream &&) = delete;

    ///
    /// \brief Create and start the debug output stream task.
    ///
    /// Creates a FreeRTOS task that manages debug output stream messages and
    /// sends BLE notifications to connected clients.
    ///
    /// \return BaseType_t pdPASS if task created successfully, pdFAIL otherwise
    ///
    BaseType_t task_create() noexcept;

    ///
    /// \brief      Check if the debug notification stream is enabled.
    ///
    /// Returns true only if:
    /// - BLE is connected
    /// - persistent_data.debug_notify_stream_enable is true (TODO)
    ///
    /// \return     true if notifications can be sent, false otherwise
    ///
    bool is_enabled() noexcept;

private:
    debug_stream() = default;

    static void task_trampoline(void *args);

    ///
    /// \brief FreeRTOS loop that periodically drains the ring buffer and sends
    ///        BLE notifications.
    ///
    void run();

    TaskHandle_t m_handle{nullptr}; ///< FreeRTOS task handle.
};

} // namespace sentinel::task

#endif /* SENTINEL_TASK_DEBUG_STREAM_HPP */
