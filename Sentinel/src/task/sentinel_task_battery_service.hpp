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
#include <FreeRTOS.h>
#include <cyhal_timer.h>
#include <task.h>
}
#pragma GCC diagnostic pop

namespace sentinel::task {

///
/// \brief Single-owner FreeRTOS task that periodically updates the battery
///        level and sends BLE notifications.
///
/// \details OO/class style, mirroring \ref sentinel::task::spi_bus — the task's
///          state (the HAL timer, the task handle) lives in private members
///          rather than \c .cpp file-static globals, and the loop runs as a
///          private \ref run reached via a static trampoline. Use the
///          \ref instance singleton — the timer ISR captures the instance via
///          its callback argument, so the object must have a stable address.
///
/// \note    This class is non-copyable and non-movable.
///
class battery_service {
public:
    ///
    /// \brief The single battery-service instance.
    ///
    /// \return Reference to the singleton \ref battery_service instance.
    ///
    static battery_service &instance() noexcept;

    /// Non-copyable, non-movable: the task/ISR entry points capture \c this.
    battery_service(const battery_service &) = delete;
    battery_service &operator=(const battery_service &) = delete;
    battery_service(battery_service &&) = delete;
    battery_service &operator=(battery_service &&) = delete;

    ///
    /// \brief Create and start the battery service task.
    ///
    /// Creates a FreeRTOS task that manages battery level updates and sends
    /// BLE notifications to connected clients.
    ///
    /// \return BaseType_t pdPASS if task created successfully, pdFAIL otherwise
    ///
    BaseType_t task_create() noexcept;

private:
    battery_service() = default;

    /// \brief Static FreeRTOS task entry point; forwards to \ref run.
    /// \param task_parameter Unused (\c this is captured via \ref instance).
    static void task_trampoline(void *task_parameter);

    /// \brief HAL timer ISR; notifies the task to run another update cycle.
    /// \param callback_arg Unused (\c this is captured via \ref instance).
    /// \param event        Timer event that fired (unused; any event notifies).
    static void timer_isr(void *callback_arg, cyhal_timer_event_t event);

    ///
    /// \brief Battery service loop. Updates a dummy battery value every time it
    ///        is notified and sends a notification to the connected peer.
    ///
    void run();

    /// \brief Configure and start \ref m_timer to drive the update cadence.
    /// \return \c CY_RSLT_SUCCESS on success; a CYHAL error code otherwise.
    cy_rslt_t configure_timer() noexcept;

    cyhal_timer_t m_timer{};       ///< HAL timer driving the update cadence.
    TaskHandle_t  m_handle{nullptr}; ///< FreeRTOS task handle.
};

} // namespace sentinel::task

#endif /* SENTINEL_TASK_BATTERY_SERVICE_HPP */
