///
/// \file    sentinel_task_battery_service.hpp
/// \brief   Battery Service Task public interface
///
/// \details This header provides the public interface for the Battery Service
///          FreeRTOS task that manages periodic battery level updates and
///          notifications.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0 - Battery service task interface
///

#ifndef SENTINEL_TASK_BATTERY_SERVICE_HPP
#define SENTINEL_TASK_BATTERY_SERVICE_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <task.h>
}
#pragma GCC diagnostic pop

namespace sentinel::task::battery_service {

///
/// \brief Create and start the battery service task
///
/// Creates a FreeRTOS task that manages battery level updates and sends
/// BLE notifications to connected clients.
///
/// \return BaseType_t pdPASS if task created successfully, pdFAIL otherwise
///
BaseType_t task_create(void);

///
/// \brief Battery service task that updates and sends battery level
/// notifications
///
/// This task updates dummy battery value every time it is notified
/// and sends a notification to the connected peer. Created in main().
///
/// \param task_parameter Task parameter (unused)
///
/// \return void
///
void task_function(void *task_parameter);

///
/// \brief FreeRTOS task handle for battery service task
///
inline TaskHandle_t task_handle;

} // namespace sentinel::task::battery_service

#endif /* SENTINEL_TASK_BATTERY_SERVICE_HPP */
