///
/// \file    sentinel_task_bme280_service.hpp
/// \brief   BME280 telemetry sample service task (periodic, BLE-streamable)
///
/// \details Declares the BME280 sample service task. The task samples the
///          BME280 (#14) over the arbitrated I²C bus at a configurable cadence
///          (default 1 Hz), caches the most-recent compensated reading, and
///          publishes it via \ref latest for any consumer — the device
///          snapshot \c populate() (#36), the live BLE BME280 characteristic
///          (#6), and internal threshold logic.
///
///          This is the production replacement for the testbench's
///          \c continuous_read loop: instead of printing each sample to the
///          debug stream, it caches the value behind a mutex and, when a BLE
///          handler has subscribed, pushes each new sample to that handler's
///          queue so the handler can issue a GATT notification. The task borrows
///          the shared BME280 from \c sentinel::resource::context() (decision
///          #13, #38) rather than constructing its own, so the factory-
///          calibration read in the driver constructor happens exactly once.
///
/// \author  galudino
/// \date    2026-06-29
/// \version 1.0 - Initial BME280 sample service task
///

#ifndef SENTINEL_TASK_BME280_SERVICE_HPP
#define SENTINEL_TASK_BME280_SERVICE_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <task.h>
}
#pragma GCC diagnostic pop

#include <cstdint>

namespace sentinel::task {

///
/// \brief Single-owner FreeRTOS task that samples the BME280 (#14) at a
///        configurable cadence, caches the latest reading, and notifies a
///        subscribed BLE handler.
///
/// \details OO/class style, mirroring \ref sentinel::task::spi_bus: the task's
///          state (cached sample + its mutex, the notify queue, the cadence,
///          the task handle) lives in private members rather than \c .cpp
///          file-static globals, and the loop runs as a private \ref run
///          reached via a static trampoline. Use the \ref instance singleton.
///
/// \note    This class is non-copyable and non-movable.
///
class bme280_service {
public:
    ///
    /// \brief Most-recent compensated BME280 reading, in fixed-point integer
    ///        units.
    ///
    /// \details Fixed-point so the snapshot / wire representation never depends
    ///          on host float formatting (project-wide constraint — see
    ///          \c sentinel_debug_print.hpp). The \ref valid flag distinguishes
    ///          a real reading from the zero-initialized state before the first
    ///          successful sample (or after a sustained sensor failure with no
    ///          prior good reading).
    ///
    struct sample {
        uint32_t unix_timestamp{};       ///< Seconds since 1970 latched at sample time (0 if RTC has not yet ticked).
        int16_t  temperature_centi_c{};  ///< Temperature in 0.01 °C (e.g. 2345 = 23.45 °C).
        uint16_t humidity_centi_pct{};   ///< Relative humidity in 0.01 %RH.
        uint32_t pressure_pa{};          ///< Barometric pressure in Pa.
        bool     valid{false};           ///< \c true once at least one successful read has been cached.
    };

    ///
    /// \brief The single BME280-service instance.
    ///
    static bme280_service &instance() noexcept;

    /// Non-copyable, non-movable: the task entry point captures \c this.
    bme280_service(const bme280_service &) = delete;
    bme280_service &operator=(const bme280_service &) = delete;
    bme280_service(bme280_service &&) = delete;
    bme280_service &operator=(bme280_service &&) = delete;

    ///
    /// \brief Create and start the BME280 sample service task.
    ///
    /// \return \c pdPASS on success, otherwise the \c xTaskCreate failure code
    ///         (or \c pdFAIL if the cache mutex could not be created).
    ///
    BaseType_t task_create() noexcept;

    ///
    /// \brief Return a copy of the most-recent cached sample.
    ///
    /// \details Safe to call from any *task* context (not from an ISR); the
    ///          copy is taken under a short mutex hold so readers never observe
    ///          a torn multi-field update. Before the task has created its mutex
    ///          (i.e. before \ref task_create), returns a default \ref sample
    ///          with \c valid == false.
    ///
    sample latest() noexcept;

    ///
    /// \brief Set the sampling cadence at runtime.
    ///
    /// \param period_ms New period in milliseconds; floored to a small minimum
    ///                  so a runaway value cannot saturate the I²C bus.
    ///
    void set_sample_period_ms(uint32_t period_ms) noexcept;

    ///
    /// \brief Current sampling cadence in milliseconds.
    ///
    uint32_t sample_period_ms() const noexcept;

    ///
    /// \brief Attach a queue to receive a copy of every new \ref sample.
    ///
    /// \details Single-subscriber by design (Phase I). The BLE notification
    ///          handler attaches the queue it reads; the task pushes each fresh
    ///          sample with a zero timeout (dropped if the queue is full — the
    ///          handler is expected to keep up or coalesce), so a slow consumer
    ///          never stalls the sample cadence.
    ///
    /// \param queue Destination queue of \ref sample items. Must outlive the
    ///              subscription.
    /// \return \c true if the subscription was registered (\c false for a null
    ///         queue).
    ///
    bool subscribe_for_notify(QueueHandle_t queue) noexcept;

    ///
    /// \brief Detach the notify queue; the task stops pushing samples.
    ///
    void unsubscribe() noexcept;

private:
    bme280_service() = default;

    static void task_trampoline(void *task_parameter);

    ///
    /// \brief Sample loop. Samples the BME280 at \ref sample_period_ms, caches
    ///        the result, and notifies a subscribed BLE handler.
    ///
    void run();

    ///
    /// \brief Publish a fresh sample to the cache (under the mutex) and, if a
    ///        subscriber is attached, to its queue (non-blocking).
    ///
    void publish(const sample &s) noexcept;

    sample            m_latest{};               ///< Cached most-recent sample.
    SemaphoreHandle_t m_latest_mutex{nullptr};  ///< Guards m_latest's multi-field update.
    QueueHandle_t     m_notify_queue{nullptr};  ///< Optional single subscriber.
    volatile uint32_t m_period_ms{1000};        ///< Sampling cadence (ms).
    TaskHandle_t      m_handle{nullptr};         ///< FreeRTOS task handle.
};

} // namespace sentinel::task

#endif /* SENTINEL_TASK_BME280_SERVICE_HPP */
