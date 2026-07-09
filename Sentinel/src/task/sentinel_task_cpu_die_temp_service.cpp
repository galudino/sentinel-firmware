///
/// \file    sentinel_task_cpu_die_temp_service.cpp
/// \brief   CPU on-die temperature service task implementation (#6/#55)
///
/// \author  galudino
/// \date    2026-07-09
/// \version 1.0
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <FreeRTOS.h>
#include <task.h>
}
#pragma GCC diagnostic pop

#include "sentinel_debug_print.hpp"
#include "sentinel_gatt_system.hpp"
#include "sentinel_psoc6_die_temperature.hpp"
#include "sentinel_task_bme280_service.hpp"
#include "sentinel_task_cpu_die_temp_service.hpp"
#include "sentinel_task_rtc_service.hpp"

#include <cstdint>

namespace sentinel::task {

namespace {

/// \brief Floor for the sampling cadence (ms).
constexpr uint32_t MIN_PERIOD_MS = 250;

/// \brief Split a signed centi-value into sign / whole / two-digit fraction so
///        `%c%d.%02d` never prints strings like "-23.-05".
void split_centi(int32_t centi, char &sign, int32_t &whole,
                 int32_t &frac) noexcept {
    sign = centi < 0 ? '-' : '+';
    const int32_t mag = centi < 0 ? -centi : centi;
    whole = mag / 100;
    frac = mag % 100;
}

} // namespace

cpu_die_temp_service &cpu_die_temp_service::instance() noexcept {
    static cpu_die_temp_service service;
    return service;
}

BaseType_t cpu_die_temp_service::task_create(UBaseType_t priority,
                                             uint16_t stack_words,
                                             uint32_t period_ms) noexcept {
    set_period_ms(period_ms);
    return xTaskCreate(&cpu_die_temp_service::task_trampoline,
                       "CPU Die-Temp Service", stack_words, this, priority,
                       &m_handle);
}

void cpu_die_temp_service::set_period_ms(uint32_t period_ms) noexcept {
    m_period_ms = period_ms < MIN_PERIOD_MS ? MIN_PERIOD_MS : period_ms;
}

uint32_t cpu_die_temp_service::period_ms() const noexcept {
    return m_period_ms;
}

void cpu_die_temp_service::task_trampoline(void *task_parameter) {
    static_cast<cpu_die_temp_service *>(task_parameter)->run();
}

void cpu_die_temp_service::run() {
    auto &die = sentinel::drivers::psoc6_die_temperature::instance();

    while (true) {
        die.refresh();

        int16_t die_centi = 0;
        if (die.cached_centi_c(die_centi)) {
            // Publish to the System CPU Temperature characteristic (R/Notify).
            sentinel::gatt::system::publish_cpu_temperature(die_centi);

            // Comparison log (#55 AC): die vs BME280 ambient vs DS3231 die.
            char die_sign, bme_sign, rtc_sign;
            int32_t die_whole, die_frac, bme_whole, bme_frac, rtc_whole,
                rtc_frac;
            split_centi(die_centi, die_sign, die_whole, die_frac);

            const auto bme = bme280_service::instance().latest();
            const auto rtc_centi =
                rtc_service::instance().last_temperature_centi_c();
            split_centi(bme.temperature_centi_c, bme_sign, bme_whole, bme_frac);
            split_centi(rtc_centi, rtc_sign, rtc_whole, rtc_frac);

            // Delta of CPU die above BME280 ambient (only meaningful once the
            // BME280 has a valid cached sample).
            const int32_t delta_centi =
                die_centi - static_cast<int32_t>(bme.temperature_centi_c);
            char d_sign;
            int32_t d_whole, d_frac;
            split_centi(delta_centi, d_sign, d_whole, d_frac);

            if (bme.valid) {
                logi("cpu_die_temp: die=%c%ld.%02ld C  bme280=%c%ld.%02ld C  "
                     "ds3231=%c%ld.%02ld C  (die-ambient=%c%ld.%02ld C)",
                     die_sign, static_cast<long>(die_whole),
                     static_cast<long>(die_frac), bme_sign,
                     static_cast<long>(bme_whole), static_cast<long>(bme_frac),
                     rtc_sign, static_cast<long>(rtc_whole),
                     static_cast<long>(rtc_frac), d_sign,
                     static_cast<long>(d_whole), static_cast<long>(d_frac));
            } else {
                logi("cpu_die_temp: die=%c%ld.%02ld C  ds3231=%c%ld.%02ld C  "
                     "(bme280 ambient not yet cached)",
                     die_sign, static_cast<long>(die_whole),
                     static_cast<long>(die_frac), rtc_sign,
                     static_cast<long>(rtc_whole), static_cast<long>(rtc_frac));
            }
        } else {
            logw("cpu_die_temp: no reading yet (sensor not initialized?)");
        }

        vTaskDelay(pdMS_TO_TICKS(m_period_ms));
    }
}

} // namespace sentinel::task
