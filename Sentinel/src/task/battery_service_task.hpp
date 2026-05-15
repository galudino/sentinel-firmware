///
/// \file    battery_service_task.hpp
/// \brief   Battery Service Task public interface
///
/// \details This header provides the public interface for the Battery Service
///          FreeRTOS task that manages periodic battery level updates and
///          notifications.
///
/// \author  galudino
/// \date    2025
/// \version 1.0 - Battery service task interface
///

#ifndef BATTERY_SERVICE_TASK_HPP
#define BATTERY_SERVICE_TASK_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <task.h>
}
#pragma GCC diagnostic pop

///
/// \brief Create and start the battery service task
///
/// Creates a FreeRTOS task that manages battery level updates and sends
/// BLE notifications to connected clients.
///
/// \return BaseType_t pdPASS if task created successfully, pdFAIL otherwise
///
BaseType_t battery_service_task_create(void);

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
void battery_service_task(void *task_parameter);

///
/// \brief FreeRTOS task handle for battery service task
///
inline TaskHandle_t battery_service_task_handle;

#endif /* BATTERY_SERVICE_TASK_HPP */
