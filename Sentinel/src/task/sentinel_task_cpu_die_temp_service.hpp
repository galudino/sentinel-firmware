///
/// \file    sentinel_task_cpu_die_temp_service.hpp
/// \brief   CPU on-die temperature service task (periodic, BLE-streamable) (#6)
///
/// \details Declares the CPU die-temperature service task: a low-priority
///          FreeRTOS task that samples the PSoC 6 on-die temperature
///          (\ref sentinel::drivers::psoc6_die_temperature) at a configurable
///          cadence, publishes it to the \c System \c CPU \c Temperature GATT
///          characteristic (R/Notify, #6 — same shape as the BME280 / DS3231
///          sensor characteristics), and logs it next to the BME280 ambient and
///          DS3231 die readings so the three can be compared on the bench
///          (an acceptance criterion of #55).
///
///          OO/class singleton, mirroring \ref sentinel::task::rtc_service and
///          \ref sentinel::task::bme280_service (decision #16): cadence + handle
///          live in private members and the loop runs as a private \ref run
///          reached via a static trampoline. Use the \ref instance singleton.
///
/// \author  galudino
/// \date    2026-07-09
/// \version 1.0 - Initial CPU die-temperature service task
///

#ifndef SENTINEL_TASK_CPU_DIE_TEMP_SERVICE_HPP
#define SENTINEL_TASK_CPU_DIE_TEMP_SERVICE_HPP

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
/// \brief Single-owner FreeRTOS task that samples the PSoC 6 die temperature,
///        publishes it to the \c System \c CPU \c Temperature characteristic, and
///        logs it against the BME280 / DS3231 readings for comparison (#6/#55).
///
/// \note This class is non-copyable and non-movable.
///
class cpu_die_temp_service {
public:
    /// \brief Default sampling cadence (~2 s) — the die temperature drifts slowly.
    static constexpr uint32_t DEFAULT_PERIOD_MS = 2000;

    /// \brief The single CPU-die-temperature-service instance.
    static cpu_die_temp_service &instance() noexcept;

    cpu_die_temp_service(const cpu_die_temp_service &) = delete;
    cpu_die_temp_service &operator=(const cpu_die_temp_service &) = delete;
    cpu_die_temp_service(cpu_die_temp_service &&) = delete;
    cpu_die_temp_service &operator=(cpu_die_temp_service &&) = delete;

    ///
    /// \brief Create and start the CPU die-temperature service task.
    ///
    /// \param priority    FreeRTOS priority (default low — not latency-critical).
    /// \param stack_words Stack depth in words.
    /// \param period_ms   Initial sampling cadence in milliseconds.
    /// \return \c pdPASS on success, otherwise the \c xTaskCreate failure code.
    ///
    BaseType_t task_create(
        UBaseType_t priority = static_cast<UBaseType_t>(configMAX_PRIORITIES - 4),
        uint16_t stack_words = static_cast<uint16_t>(configMINIMAL_STACK_SIZE * 4),
        uint32_t period_ms = DEFAULT_PERIOD_MS) noexcept;

    /// \brief Set the sampling cadence at runtime (floored to a small minimum).
    void set_period_ms(uint32_t period_ms) noexcept;

    /// \brief Current sampling cadence in milliseconds.
    uint32_t period_ms() const noexcept;

private:
    cpu_die_temp_service() = default;

    static void task_trampoline(void *task_parameter);

    /// \brief Sample → publish (GATT) → comparison-log → delay, forever.
    void run();

    volatile uint32_t m_period_ms{DEFAULT_PERIOD_MS}; ///< Sampling cadence (ms).
    TaskHandle_t      m_handle{nullptr};              ///< FreeRTOS task handle.
};

} // namespace sentinel::task

#endif /* SENTINEL_TASK_CPU_DIE_TEMP_SERVICE_HPP */
