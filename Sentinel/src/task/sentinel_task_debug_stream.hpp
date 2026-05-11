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
#include <task.h>
}
#pragma GCC diagnostic pop

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace sentinel::task::debug_stream {

///
/// \brief Create and start the debug output stream task
///
/// Creates a FreeRTOS task that manages debug output stream messages and sends
/// BLE notifications to connected clients.
///
/// \return BaseType_t pdPASS if task created successfully, pdFAIL otherwise
///
BaseType_t task_create(void);

///
/// \brief      FreeRTOS task that periodically drains the ring buffer
///             and sends BLE notifications.
///
/// \param      args    Not used (required by FreeRTOS task signature)
///
void task_function(void *args);

///
/// \brief      Check if the debug notification stream is enabled.
///
/// Returns true only if:
/// - BLE is connected
/// - persistent_data.debug_notify_stream_enable is true (TODO)
///
/// \return     true if notifications can be sent, false otherwise
///
bool is_enabled(void);

///
/// \brief FreeRTOS task handle for debug stream task
///
inline TaskHandle_t task_handle;

} // namespace sentinel::task::debug_stream

#endif /* SENTINEL_TASK_DEBUG_STREAM_HPP */
