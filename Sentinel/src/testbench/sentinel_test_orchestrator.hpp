///
/// \file    sentinel_test_orchestrator.hpp
/// \brief   One-shot serial test orchestrator task (#48)
///
/// \details Declares the testbench's serial test orchestrator: a single
///          highest-priority, one-shot FreeRTOS task that drives the whole
///          test sequence \b bottom-up and \b serially so the retarget-IO
///          serial log reads top-to-bottom as a diagnostic (issue #48).
///
///          It is the testbench twin of #38's boot orchestrator (decision #13):
///          the only things created before \c vTaskStartScheduler() are the bus
///          arbiters (in \c resource::peripheral_initialize()) and the BLE
///          debug-stream task; this orchestrator — created pre-scheduler but
///          running only \b after the scheduler starts, so the arbiters can
///          pump its I/O — spawns and starts everything else. In order it:
///
///          1. prints a banner;
///          2. runs each driver / component test group \b to completion in
///             dependency order (BME280 → DS3231 → W25Q128 → record-store →
///             system-event-log → device-snapshot → POST → snapshot-stream),
///             each via the suite's synchronous \c run_all();
///          3. prints a per-group pass/fail tally summary;
///          4. \b then creates and starts the continuous reader services
///             (\c rtc_service, \c bme280_service);
///          5. self-deletes.
///
///          Gating between groups is by \b direct synchronous calls — no
///          inter-task notification handshakes (issue #48 implementation note).
///
///          OO/class style, mirroring the task layer (decision #16): the task
///          handle lives in a private member and the sequence runs as a private
///          \ref sentinel::testbench::test_orchestrator::run reached through a
///          static trampoline.
///
/// \author  galudino
/// \date    2026-06-30
/// \version 1.0 - Initial serial test orchestrator
///

#ifndef SENTINEL_TEST_ORCHESTRATOR_HPP
#define SENTINEL_TEST_ORCHESTRATOR_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

namespace sentinel::testbench {

///
/// \brief Single-owner one-shot task that runs the testbench suite serially.
///
/// \details Use the \ref instance singleton. Created (idle) by
///          \c sentinel::testbench::initialize() before the scheduler starts;
///          its \ref sentinel::testbench::test_orchestrator::run body
///          executes once the scheduler is running, then the task
///          self-deletes.
///
/// \note    This class is non-copyable and non-movable.
///
class test_orchestrator {
public:
    ///
    /// \brief Highest priority below the bus arbiters (\c configMAX_PRIORITIES
    ///        - 2), so the orchestrator runs to completion ahead of the
    ///        \c configMAX_PRIORITIES - 3 reader services, while still yielding
    ///        to the same-priority arbiters whenever it blocks on bus I/O.
    ///
    static constexpr UBaseType_t DEFAULT_PRIORITY =
        static_cast<UBaseType_t>(configMAX_PRIORITIES - 2);

    ///
    /// \brief Stack depth: the driver suites allocate scratch buffers and deep
    ///        logging frames, so mirror their 6× minimum.
    ///
    static constexpr uint16_t DEFAULT_STACK_WORDS =
        static_cast<uint16_t>(configMINIMAL_STACK_SIZE * 6);

    ///
    /// \brief The single orchestrator instance.
    ///
    /// \return Reference to the process-wide \ref test_orchestrator.
    ///
    static test_orchestrator &instance() noexcept;

    /// Non-copyable, non-movable: the task entry point captures \c this.
    test_orchestrator(const test_orchestrator &) = delete;
    test_orchestrator &operator=(const test_orchestrator &) = delete;
    test_orchestrator(test_orchestrator &&) = delete;
    test_orchestrator &operator=(test_orchestrator &&) = delete;

    ///
    /// \brief Create the one-shot orchestrator task.
    ///
    /// \param ble_stack_ok \c true if \c stack_initialize() succeeded (captured
    ///                     in \c initialize()).
    /// \param gatt_db_ok   \c true if the GATT database registered. Phase I has
    ///                     no custom GATT DB yet (#6), so the caller passes the
    ///                     stack-init result; #6 threads the real value.
    /// \param priority    FreeRTOS task priority.
    /// \param stack_words Stack depth in words.
    ///
    /// \return \c pdPASS on success, otherwise the \c xTaskCreate failure code.
    ///
    BaseType_t task_create(bool ble_stack_ok, bool gatt_db_ok,
                           UBaseType_t priority = DEFAULT_PRIORITY,
                           uint16_t stack_words = DEFAULT_STACK_WORDS) noexcept;

private:
    test_orchestrator() = default;

    ///
    /// \brief FreeRTOS task entry point: recovers \c this and calls
    ///        \ref sentinel::testbench::test_orchestrator::run.
    ///
    /// \param task_parameter The \ref test_orchestrator instance, passed as
    ///                       the \c pvParameters argument to \c xTaskCreate.
    ///
    static void task_trampoline(void *task_parameter);

    ///
    /// \brief Run the full bottom-up serial test sequence, then hand off to the
    ///        continuous readers and self-delete.
    ///
    void run();

    bool m_ble_stack_ok{false};     ///< Captured BLE stack-init result.
    bool m_gatt_db_ok{false};       ///< Captured GATT-DB registration result.
    TaskHandle_t m_handle{nullptr}; ///< FreeRTOS task handle.
};

} // namespace sentinel::testbench

#endif /* SENTINEL_TEST_ORCHESTRATOR_HPP */
