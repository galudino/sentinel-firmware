///
/// \file    sentinel_task_rtc_service.cpp
/// \brief   DS3231 RTC service task implementation
///
/// \details Implements the RTC service task declared in
///          \c sentinel_task_rtc_service.hpp. The task configures the DS3231
///          for 1 Hz square-wave output on its INT/SQW pin, arms a
///          falling-edge GPIO interrupt on \ref
///          sentinel::resource::rtc_sqw_pin, and latches the current time on
///          each edge.
///
///          ISR discipline mirrors \c sentinel_task_battery_service.cpp: the
///          GPIO callback does nothing but notify the task and yield; the
///          blocking I²C read happens in task context, since the bus is
///          arbitrated through a FreeRTOS queue and cannot be touched from
///          an ISR.
///
/// \author  galudino
/// \date    2026-05-24
/// \version 1.0 - Initial RTC service task implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_log.h"
#include "cy_result.h"
#include "cyhal_gpio.h"
#include "portmacro.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include "sentinel_cyhal_i2c_bus_transport.hpp"
#include "sentinel_debug_print.hpp"
#include "sentinel_ds3231.hpp"
#include "sentinel_resource.hpp"
#include "sentinel_task_rtc_service.hpp"
#include "sentinel_utilities.hpp"

#include <cstdint>
#include <optional>

namespace {

///
/// \brief Concrete DS3231 instantiation over the bus-arbitrated transport.
///
using ds3231_t = sentinel::ds3231<sentinel::cyhal_i2c_bus_transport>;

///
/// \brief NVIC priority for the SQW GPIO interrupt.
///
/// \details Matches the value the battery service uses for its timer event.
///          Numerically above (lower urgency than) the FreeRTOS syscall
///          ceiling, so calling \c vTaskNotifyGiveFromISR from the handler
///          is permitted.
///
constexpr uint8_t SQW_IRQ_PRIORITY = 3;

///
/// \brief How often the latched time/temperature is logged, in seconds.
///
/// \details The SQW interrupt and the \ref last_unix_time latch always run at
///          1 Hz; this only throttles log output. The testbench logs every
///          tick so the 1 Hz interrupt is visibly confirmed on the serial
///          monitor; the application logs once per minute to keep the serial
///          and BLE debug streams quiet. Must evenly divide 60.
///
#if defined(SENTINEL_TESTBENCH)
constexpr uint8_t HEARTBEAT_LOG_PERIOD_SECONDS = 1;
#else
constexpr uint8_t HEARTBEAT_LOG_PERIOD_SECONDS = 60;
#endif

///
/// \brief ISO-day-of-week (1=Mon … 7=Sun) → short label, for log lines.
///
inline const char *day_name(uint8_t iso_dow) noexcept {
    static constexpr const char *names[8] = {"?",   "Mon", "Tue", "Wed",
                                             "Thu", "Fri", "Sat", "Sun"};
    return (iso_dow >= 1 && iso_dow <= 7) ? names[iso_dow] : names[0];
}

///
/// \brief Format a signed centi-unit integer with two decimal places.
///
/// \details Helper that pulls the sign apart from the magnitude so the
///          \c %d.%02d trick does not produce strings like \c "-23.-05".
///
/// \param[in]  centi      Value scaled by 100 (e.g. \c -2305 for \c -23.05).
/// \param[out] sign_out   Receives \c '-' for negatives, \c '+' otherwise.
/// \param[out] whole_out  Receives the absolute-value whole part.
/// \param[out] frac_out   Receives the absolute-value two-digit fraction.
///
inline void split_centi(int32_t centi, char &sign_out, int32_t &whole_out,
                        int32_t &frac_out) noexcept {
    sign_out = centi < 0 ? '-' : '+';

    auto magnitude = centi < 0 ? -centi : centi;
    whole_out = magnitude / 100;
    frac_out = magnitude % 100;
}

///
/// \brief Program the DS3231 to emit a 1 Hz square wave on INT/SQW.
///
/// \param rtc Driver bound to the DS3231.
/// \return \c true if both the rate-select and INTCN writes succeeded.
///
bool configure_square_wave(ds3231_t &rtc) noexcept {
    // Select 1 Hz (RS2:RS1 = 00), then route the pin to square-wave mode
    // (INTCN = 0). Order does not matter; both are control-register
    // read-modify-writes.
    return rtc.set_square_wave_freq(ds3231_t::square_wave_freq::hz_1) &&
           rtc.set_int_sqw_mode(ds3231_t::int_sqw_mode::square_wave);
}

} // namespace

using namespace sentinel::task;

rtc_service &rtc_service::instance() noexcept {
    static rtc_service service;
    return service;
}

uint32_t rtc_service::last_unix_time() const noexcept {
    return m_last_unix_seconds;
}

int16_t rtc_service::last_temperature_centi_c() const noexcept {
    return m_last_temperature_centi;
}

BaseType_t rtc_service::task_create() noexcept {
    return xTaskCreate(&rtc_service::task_trampoline, "RTC Service Task",
                       (configMINIMAL_STACK_SIZE * 4), this,
                       (configMAX_PRIORITIES - 3), &m_handle);
}

void rtc_service::task_trampoline(void *task_parameter) {
    static_cast<rtc_service *>(task_parameter)->run();
}

///
/// \brief SQW falling-edge interrupt handler.
///
/// \details Kept minimal: unblock the service task and request a context
///          switch if it is now the highest-priority ready task. All bus
///          work happens back in task context. The instance is recovered from
///          the callback argument stored at registration time.
///
void rtc_service::sqw_event_isr(void *callback_arg,
                                cyhal_gpio_event_t event) {
    sentinel::unused(event);

    auto *self = static_cast<rtc_service *>(callback_arg);

    auto higher_priority_task_woken = BaseType_t{pdFALSE};
    vTaskNotifyGiveFromISR(self->m_handle, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

///
/// \brief Arm the falling-edge interrupt on the SQW pin.
///
/// \details The pin is owned by Device Configurator (\c CYBSP_RTC_SQW:
///          input, pull-up): \c cybsp_init both initializes it via
///          \c Cy_GPIO_Pin_Init and pin-reserves it through the HAL hardware
///          manager. We must therefore NOT call \c cyhal_gpio_init here — it
///          would fail with an in-use error against that reservation.
///          Registering the callback and enabling the event act directly on
///          the port's interrupt registers and NVIC line, independent of the
///          HAL init/reservation, so they are all that is needed. The
///          pull-up drive mode lets the DS3231's open-drain SQW idle high and
///          pull low each half-cycle, giving a clean falling edge.
///
void rtc_service::configure_sqw_interrupt() noexcept {
    m_sqw_callback_data.callback = &rtc_service::sqw_event_isr;
    m_sqw_callback_data.callback_arg = this;
    cyhal_gpio_register_callback(sentinel::resource::rtc_sqw_pin,
                                 &m_sqw_callback_data);
    cyhal_gpio_enable_event(sentinel::resource::rtc_sqw_pin,
                            CYHAL_GPIO_IRQ_FALL, SQW_IRQ_PRIORITY, true);
}

void rtc_service::run() {
    // Bus transport + driver are task-local: they live for the whole task
    // lifetime (this loop never returns on the happy path) and are not shared
    // with other tasks. Routes through sentinel::resource::cybsp_i2c_bus so the
    // per-second reads serialize cleanly with every other task on the shared
    // I²C bus.
    auto rtc_bus = sentinel::cyhal_i2c_bus_transport(
        sentinel::resource::cybsp_i2c_bus,
        static_cast<uint16_t>(ds3231_t::slave_address::primary));

    auto rtc = ds3231_t(rtc_bus);

    if (!configure_square_wave(rtc)) {
        loge("rtc_service: 1 Hz SQW config failed (last_err=%d)",
             static_cast<int>(rtc.last_error()));
        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                   "RTC service: 1 Hz SQW config failed (last_err=%d)\n",
                   static_cast<int>(rtc.last_error()));
        vTaskDelete(nullptr);
        return;
    }

    configure_sqw_interrupt();

    logi("rtc_service: 1 Hz SQW armed (falling-edge IRQ on P6_3)", "");
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "RTC service: 1 Hz SQW armed (falling-edge IRQ on P6_3)\n");

    while (true) {
        // Block until the next SQW falling edge (one per second).
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        auto now = rtc.time();
        if (!now) {
            loge("rtc_service: time read error %d",
                 static_cast<int>(rtc.last_error()));
            continue;
        }

        // Latch the timestamp first: it is this task's primary product and
        // must not be gated on the best-effort temperature read below.
        auto unix = ds3231_t::datetime::to_unix_time(*now);
        if (unix) {
            m_last_unix_seconds = *unix;
        }

        // Throttle logging to HEARTBEAT_LOG_PERIOD_SECONDS; the time latch
        // above always runs at 1 Hz regardless. Temperature is only read here,
        // when a line is actually about to be logged.
        if (now->second % HEARTBEAT_LOG_PERIOD_SECONDS != 0) {
            continue;
        }

        auto temp = rtc.temperature_centi_c();
        if (!temp) {
            loge("rtc_service: temperature read error %d",
                 static_cast<int>(rtc.last_error()));
            cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_ERR,
                       "rtc_service: temperature error %d\n",
                       static_cast<int>(rtc.last_error()));
            continue;
        }

        // Publish the latest temperature for cross-task consumers (the device
        // snapshot #36 reads this cache rather than issuing its own I²C read).
        m_last_temperature_centi = *temp;

        auto sign = char{};
        auto whole = int32_t{};
        auto frac = int32_t{};
        split_centi(*temp, sign, whole, frac);

        logi("%04d-%02d-%02d %s %02d:%02d:%02d  T=%c%d.%02d C",
             static_cast<int>(now->year), static_cast<int>(now->month),
             static_cast<int>(now->date), day_name(now->day_of_week),
             static_cast<int>(now->hour), static_cast<int>(now->minute),
             static_cast<int>(now->second), sign, static_cast<int>(whole),
             static_cast<int>(frac));

        cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
                   "rtc_service: %04d-%02d-%02d %s %02d:%02d:%02d  "
                   "T=%c%d.%02d C\n",
                   static_cast<int>(now->year), static_cast<int>(now->month),
                   static_cast<int>(now->date), day_name(now->day_of_week),
                   static_cast<int>(now->hour), static_cast<int>(now->minute),
                   static_cast<int>(now->second), sign, static_cast<int>(whole),
                   static_cast<int>(frac));
    }
}
