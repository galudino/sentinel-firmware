///
/// \file    sentinel_app_orchestrator.hpp
/// \brief   Production boot orchestrator task (decision #13, issue #38)
///
/// \details Declares the application's boot orchestrator: a single highest-
///          priority, one-shot FreeRTOS task that stands up the real boot path
///          in dependency order, then hands off to the long-running service
///          tasks and self-deletes. It is the production twin of the
///          testbench's serial test orchestrator (#48); both exist because the
///          only things that can be created before \c vTaskStartScheduler() are
///          the bus arbiters and the BLE debug-stream task — everything that
///          does bus I/O (POST probes, the BME280 calibration read in the
///          shared device context, the flash-region scans) needs the arbiters
///          pumping, which only happens once the scheduler runs.
///
///          In order, \ref run:
///            1. builds the shared device context (\c resource::context()) and
///               scans both flash record stores (\c initialize_stores());
///            2. runs POST against the real drivers, caches the first-failure
///               status, and records the summary to the System Event Log;
///            3. starts the event-log drain task — whose boot sequence appends
///               \c shutdown_unexpected / \c boot_complete;
///            4. starts the service tasks (RTC, BME280 sample, snapshot
///               persistence (lane 1), snapshot stream (lane 2, idle),
///               battery);
///            5. self-deletes.
///
///          OO/class style (decision #16); use the \ref instance singleton.
///
/// \author  galudino
/// \date    2026-06-30
/// \version 1.0 - Initial production boot orchestrator
///

#ifndef SENTINEL_APP_ORCHESTRATOR_HPP
#define SENTINEL_APP_ORCHESTRATOR_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

namespace sentinel::app {

///
/// \brief Single-owner one-shot task that runs the production boot sequence.
///
/// \details Created (idle) by \c create_tasks() before the scheduler starts;
/// its
///          \ref run body executes once the scheduler is running, then the task
///          self-deletes. Non-copyable, non-movable.
///
class boot_orchestrator {
public:
    ///
    /// \brief Highest priority below the bus arbiters
    ///        (\c configMAX_PRIORITIES - 2), so the orchestrator runs to
    ///        completion ahead of the \c configMAX_PRIORITIES - 3 service tasks
    ///        while still yielding to the same-priority arbiters when it blocks
    ///        on bus I/O. Mirrors the testbench orchestrator (#48).
    ///
    static constexpr UBaseType_t DEFAULT_PRIORITY =
        static_cast<UBaseType_t>(configMAX_PRIORITIES - 2);

    ///
    /// \brief Stack depth: POST + the device-context construction issue deep
    ///        driver / logging frames, so mirror the suites' 6× minimum.
    ///
    static constexpr uint16_t DEFAULT_STACK_WORDS =
        static_cast<uint16_t>(configMINIMAL_STACK_SIZE * 6);

    ///
    /// \brief The single orchestrator instance.
    ///
    static boot_orchestrator &instance() noexcept;

    boot_orchestrator(const boot_orchestrator &) = delete;
    boot_orchestrator &operator=(const boot_orchestrator &) = delete;
    boot_orchestrator(boot_orchestrator &&) = delete;
    boot_orchestrator &operator=(boot_orchestrator &&) = delete;

    ///
    /// \brief Create the one-shot orchestrator task.
    ///
    /// \param ble_stack_ok \c true if \c stack_initialize() succeeded (captured
    ///                     in \c initialize()).
    /// \param gatt_db_ok   \c true if the GATT database registered. Phase I has
    ///                     no custom GATT DB yet (#6), so the caller passes the
    ///                     stack-init result; #6 threads the real value.
    /// \param priority     FreeRTOS task priority.
    /// \param stack_words  Stack depth in words.
    /// \return \c pdPASS on success, otherwise the \c xTaskCreate failure code.
    ///
    BaseType_t task_create(bool ble_stack_ok, bool gatt_db_ok,
                           UBaseType_t priority = DEFAULT_PRIORITY,
                           uint16_t stack_words = DEFAULT_STACK_WORDS) noexcept;

private:
    boot_orchestrator() = default;

    static void task_trampoline(void *task_parameter);

    ///
    /// \brief Run the boot sequence, start the service tasks, self-delete.
    ///
    void run();

    bool m_ble_stack_ok{false};     ///< Captured BLE stack-init result.
    bool m_gatt_db_ok{false};       ///< Captured GATT-DB registration result.
    TaskHandle_t m_handle{nullptr}; ///< FreeRTOS task handle.
};

} // namespace sentinel::app

#endif /* SENTINEL_APP_ORCHESTRATOR_HPP */
