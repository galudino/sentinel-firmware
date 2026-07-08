///
/// \file    sentinel_task_bme280_service.cpp
/// \brief   BME280 telemetry sample service task implementation
///
/// \details Implements the sample service task declared in
///          \c sentinel_task_bme280_service.hpp. The task owns a BME280 driver
///          over the bus-arbitrated I²C transport, samples it at a configurable
///          cadence (default 1 Hz), caches the most-recent compensated reading
///          behind a mutex, and — when a BLE handler has subscribed — pushes
///          each fresh sample to that handler's queue.
///
///          Design mirrors \c sentinel_task_rtc_service.cpp: the driver
///          instance is task-local, the cached product is published for other
///          tasks to consume, and all bus traffic happens in task context (the
///          arbiter cannot be touched from an ISR). A read failure is logged
///          but never fatal — the loop keeps running so the task survives a
///          sensor that is absent at boot and appears later, and \ref latest
///          keeps reporting the last good reading (or \c valid == false until
///          the first success).
///
/// \author  galudino
/// \date    2026-06-29
/// \version 1.0 - Initial BME280 sample service task implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "bme280.h"
#include "bme280_defs.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_bme280.hpp"
#include "sentinel_cyhal_i2c_bus_transport.hpp"
#include "sentinel_debug_print.hpp"
#include "sentinel_device_context.hpp"
#include "sentinel_gatt_bme280.hpp"
#include "sentinel_resource.hpp"
#include "sentinel_task_bme280_service.hpp"
#include "sentinel_task_rtc_service.hpp"
#include "sentinel_utilities.hpp"

#include <cstdint>
#include <optional>

namespace {

///
/// \brief Concrete BME280 instantiation over the bus-arbitrated transport.
///
using bme280_t = sentinel::bme280_i2c<sentinel::cyhal_i2c_bus_transport>;

///
/// \brief Floor on the cadence. Below the BME280's effective conversion time
///        for the configured oversampling, extra reads return no new data and
///        only burn I²C bandwidth.
///
constexpr uint32_t MIN_PERIOD_MS = 125;

///
/// \brief How often the heartbeat sample line is logged, in samples.
///
/// \details The cache + notify always run every sample; this only throttles
///          log output. The testbench logs every sample so the cadence is
///          visibly confirmed on the serial monitor; the application logs once
///          per ~minute to keep the serial and BLE debug streams quiet.
///
#if defined(SENTINEL_TESTBENCH)
constexpr uint32_t HEARTBEAT_LOG_EVERY_N = 1;
#else
constexpr uint32_t HEARTBEAT_LOG_EVERY_N = 60;
#endif

///
/// \brief Format a signed centi-unit integer with two decimal places.
///
/// \details Pulls the sign apart from the magnitude so the \c %d.%02d trick
///          does not produce strings like \c "-23.-05".
///
inline void split_centi(int32_t centi, char &sign_out, int32_t &whole_out,
                        int32_t &frac_out) noexcept {
    sign_out = centi < 0 ? '-' : '+';

    auto magnitude = centi < 0 ? -centi : centi;
    whole_out = magnitude / 100;
    frac_out = magnitude % 100;
}

///
/// \brief Convert a Bosch compensated reading into the fixed-point cache
/// sample.
///
/// \details The Bosch driver is compiled in double-precision compensation mode,
///          so \c temperature / \c humidity / \c pressure arrive as doubles in
///          °C, %RH, and Pa. Scale to the struct's integer units; this is the
///          only place doubles touch the data path (no \c %f formatting — see
///          \c sentinel_debug_print.hpp).
///
sentinel::task::bme280_service::sample
build_sample(const bme280_data &data, uint32_t unix_timestamp) noexcept {
    auto s = sentinel::task::bme280_service::sample{};
    s.unix_timestamp = unix_timestamp;
    s.temperature_centi_c = static_cast<int16_t>(data.temperature * 100.0);
    s.humidity_centi_pct = static_cast<uint16_t>(data.humidity * 100.0);
    s.pressure_pa = static_cast<uint32_t>(data.pressure);
    s.valid = true;
    return s;
}

} // namespace

using namespace sentinel::task;

bme280_service &bme280_service::instance() noexcept {
    static bme280_service service;
    return service;
}

void bme280_service::publish(const sample &s) noexcept {
    if (m_latest_mutex != nullptr) {
        xSemaphoreTake(m_latest_mutex, portMAX_DELAY);
        m_latest = s;
        xSemaphoreGive(m_latest_mutex);
    }

    if (m_notify_queue != nullptr) {
        // Zero timeout: drop the sample if the handler's queue is full rather
        // than stall the sample cadence on a slow consumer.
        xQueueSendToBack(m_notify_queue, &s, 0);
    }

    // Publish to the BME280 Ambient Sample GATT characteristic (#6): refresh the
    // read value and notify a subscribed central. A no-op on the wire when no
    // central is connected/subscribed; the notify gate lives in the gatt layer.
    if (s.valid) {
        sentinel::gatt::bme280::publish(s.temperature_centi_c,
                                        s.humidity_centi_pct, s.pressure_pa);
    }
}

bme280_service::sample bme280_service::latest() noexcept {
    auto copy = sample{};

    if (m_latest_mutex == nullptr) {
        return copy; // task not started yet → valid == false
    }

    xSemaphoreTake(m_latest_mutex, portMAX_DELAY);
    copy = m_latest;
    xSemaphoreGive(m_latest_mutex);
    return copy;
}

void bme280_service::set_sample_period_ms(uint32_t new_period_ms) noexcept {
    m_period_ms = new_period_ms < MIN_PERIOD_MS ? MIN_PERIOD_MS : new_period_ms;
}

uint32_t bme280_service::sample_period_ms() const noexcept {
    return m_period_ms;
}

bool bme280_service::subscribe_for_notify(QueueHandle_t queue) noexcept {
    if (queue == nullptr) {
        return false;
    }
    m_notify_queue = queue;
    return true;
}

void bme280_service::unsubscribe() noexcept { m_notify_queue = nullptr; }

BaseType_t bme280_service::task_create() noexcept {
    // The cache mutex must exist before any consumer can call latest(), so
    // create it here rather than inside the task body.
    m_latest_mutex = xSemaphoreCreateMutex();
    if (m_latest_mutex == nullptr) {
        return pdFAIL;
    }

    // configMINIMAL_STACK_SIZE is too lean for the Bosch double-precision
    // compensation paths plus the logging frames; the testbench BME280 task
    // settled on 4× minimum, which we mirror here.
    return xTaskCreate(&bme280_service::task_trampoline, "BME280 Service Task",
                       (configMINIMAL_STACK_SIZE * 4), this,
                       (configMAX_PRIORITIES - 3), &m_handle);
}

void bme280_service::task_trampoline(void *task_parameter) {
    static_cast<bme280_service *>(task_parameter)->run();
}

void bme280_service::run() {
    // Borrow the shared BME280 from the application device context (decision
    // #13). One driver instance means the factory-calibration read in the
    // BME280 constructor happens once (at context construction) rather than
    // once per consumer. The context is built by the boot orchestrator
    // (post-scheduler) before this task starts; its transport still routes
    // through sentinel::resource::cybsp_i2c_bus so the periodic reads serialize
    // cleanly with every other task on the shared I²C bus (notably the DS3231).
    auto &sensor = sentinel::resource::context().bme;

    // Gate-check the part is present. A failure here is logged but NOT fatal:
    // the loop still runs so the task survives a sensor that is absent at boot
    // and appears later (resilience), and latest() keeps reporting
    // valid == false until a real reading lands.
    logd("bme280_service: probing chip id");

    if (auto id = sensor.read_chip_id(); !id || *id != BME280_CHIP_ID) {
        logw(
            "bme280_service: probe failed (chip_id=0x%02X, err=%d); continuing",
            id ? static_cast<int>(*id) : 0xFF,
            static_cast<int>(sensor.last_error()));
    } else {
        logi("bme280_service: probe ok (chip_id=0x%02X), sampling at %d ms",
             static_cast<int>(*id), static_cast<int>(m_period_ms));
    }

    auto sample_counter = uint32_t{0};

    while (true) {
        auto data = sensor.read_sensor_data();

        if (!data) {
            // Keep the last good cached sample; just surface the error. Do not
            // crash — the sensor may return.
            loge("bme280_service: sample read failed (err=%d)",
                 static_cast<int>(sensor.last_error()));
        } else {
            auto s =
                build_sample(*data, rtc_service::instance().last_unix_time());
            publish(s);

            if (sample_counter % HEARTBEAT_LOG_EVERY_N == 0) {
                auto t_sign = char{};
                auto t_whole = int32_t{};
                auto t_frac = int32_t{};
                split_centi(s.temperature_centi_c, t_sign, t_whole, t_frac);

                auto h_whole = static_cast<int32_t>(s.humidity_centi_pct) / 100;
                auto h_frac = static_cast<int32_t>(s.humidity_centi_pct) % 100;

                logi("bme280_service: sample T=%c%d.%02d C  P=%d Pa  "
                     "H=%d.%02d %%",
                     t_sign, static_cast<int>(t_whole),
                     static_cast<int>(t_frac), static_cast<int>(s.pressure_pa),
                     static_cast<int>(h_whole), static_cast<int>(h_frac));
            }

            ++sample_counter;
        }

        vTaskDelay(pdMS_TO_TICKS(m_period_ms));
    }
}
