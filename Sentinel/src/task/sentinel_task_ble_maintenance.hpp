///
/// \file    sentinel_task_ble_maintenance.hpp
/// \brief   Async BLE-triggered maintenance task (#6)
///
/// \details Performs slow, GATT-write-triggered operations off the Bluetooth
///          stack callback: clearing a flash record store
///          (\c record_store::erase_all erases every sector — multi-second) and
///          entering the bootloader (a deferred device reset). The GATT write
///          handler only \e requests the work via a FreeRTOS task notification
///          (safe from the BT task context); this task performs it, so the BT
///          callback returns promptly and the write response is not stalled.
///
///          OO/class style (decision #16): request bits + handle live in
///          private members; the loop runs as a private
///          \ref sentinel::task::ble_maintenance_task::run reached via a static
///          trampoline. Use the
///          \ref sentinel::task::ble_maintenance_task::instance singleton.
///          App-only — started by the boot orchestrator, never the testbench.
///
/// \author  galudino
/// \date    2026-07-08
/// \version 1.0
///

#ifndef SENTINEL_TASK_BLE_MAINTENANCE_HPP
#define SENTINEL_TASK_BLE_MAINTENANCE_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

#include <cstdint>

namespace sentinel::task {

///
/// \brief Single-owner task that runs slow BLE-triggered maintenance off the
///        Bluetooth callback (store clears + deferred bootloader reset).
///
/// \note This class is non-copyable and non-movable.
///
class ble_maintenance_task {
public:
    /// \brief The single maintenance-task instance.
    /// \return Reference to the singleton \ref ble_maintenance_task instance.
    static ble_maintenance_task &instance() noexcept;

    ble_maintenance_task(const ble_maintenance_task &) = delete;
    ble_maintenance_task &operator=(const ble_maintenance_task &) = delete;
    ble_maintenance_task(ble_maintenance_task &&) = delete;
    ble_maintenance_task &operator=(ble_maintenance_task &&) = delete;

    /// \brief Create and start the maintenance task (idle until a request).
    /// \param priority    FreeRTOS task priority.
    /// \param stack_words Task stack size, in words.
    /// \return \c pdPASS on success, otherwise the \c xTaskCreate failure code.
    BaseType_t task_create(
        UBaseType_t priority = static_cast<UBaseType_t>(configMAX_PRIORITIES -
                                                        4),
        uint16_t stack_words = static_cast<uint16_t>(configMINIMAL_STACK_SIZE *
                                                     4)) noexcept;

    /// \brief Request an erase of the Snapshot History store (async).
    void request_clear_snapshots() noexcept;

    /// \brief Request an erase of the System Event Log store (async).
    void request_clear_events() noexcept;

    /// \brief Request entering the bootloader: a short-delayed device reset.
    void request_bootloader() noexcept;

private:
    ble_maintenance_task() = default;

    /// \brief Static FreeRTOS task entry point; forwards to \ref run.
    /// \param task_parameter Unused (\c this is captured via \ref instance).
    static void task_trampoline(void *task_parameter);

    /// \brief Maintenance loop: waits for a request bit, then services it.
    void run();

    /// \brief Set a request bit and notify the task.
    /// \param bit One of the request bits handled by \ref run (e.g. the
    ///            snapshot-clear, event-clear, or bootloader request bit).
    void notify(uint32_t bit) noexcept;

    TaskHandle_t m_handle{nullptr}; ///< FreeRTOS task handle.
};

} // namespace sentinel::task

#endif /* SENTINEL_TASK_BLE_MAINTENANCE_HPP */
