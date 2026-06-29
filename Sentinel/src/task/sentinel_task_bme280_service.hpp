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
///          queue so the handler can issue a GATT notification. The task owns
///          its own driver instance, mirroring \ref sentinel::task::rtc_service;
///          the shared-device-context refactor (drivers as
///          \c sentinel::resource singletons) lands later with the boot
///          orchestrator (#38).
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
#include <task.h>
}
#pragma GCC diagnostic pop

#include <cstdint>

namespace sentinel::task::bme280_service {

///
/// \brief Most-recent compensated BME280 reading, in fixed-point integer units.
///
/// \details Fixed-point so the snapshot / wire representation never depends on
///          host float formatting (project-wide constraint — see
///          \c sentinel_debug_print.hpp). The \ref valid flag distinguishes a
///          real reading from the zero-initialized state before the first
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
/// \brief Create and start the BME280 sample service task.
///
/// \return \c pdPASS on success, otherwise the \c xTaskCreate failure code
///         (or \c pdFAIL if the cache mutex could not be created).
///
BaseType_t task_create();

///
/// \brief Task body. Samples the BME280 at \ref sample_period_ms, caches the
///        result, and notifies a subscribed BLE handler. Created in main().
///
/// \param task_parameter Unused.
///
void task_function(void *task_parameter);

///
/// \brief FreeRTOS task handle for the sample task.
///
inline TaskHandle_t task_handle;

///
/// \brief Return a copy of the most-recent cached sample.
///
/// \details Safe to call from any *task* context (not from an ISR); the copy
///          is taken under a short mutex hold so readers never observe a torn
///          multi-field update. Before the task has created its mutex (i.e.
///          before \ref task_create), returns a default \ref sample with
///          \c valid == false.
///
sample latest() noexcept;

///
/// \brief Set the sampling cadence at runtime.
///
/// \param period_ms New period in milliseconds; floored to a small minimum so
///                  a runaway value cannot saturate the I²C bus.
///
void set_sample_period_ms(uint32_t period_ms) noexcept;

///
/// \brief Current sampling cadence in milliseconds.
///
uint32_t sample_period_ms() noexcept;

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

} // namespace sentinel::task::bme280_service

#endif /* SENTINEL_TASK_BME280_SERVICE_HPP */
