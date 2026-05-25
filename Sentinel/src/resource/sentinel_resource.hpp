///
/// \file    sentinel_resource.hpp
/// \brief   Hardware peripheral resource definitions and initialization
///
/// \details This header provides global peripheral resource handles and
///          initialization functions for UART, SPI, I2C, and PWM peripherals.
///          All resources are defined inline for application-wide access.
///
///          Each shared bus is also accompanied by its bus-arbiter task
///          singleton (e.g. \c cybsp_i2c_bus). Drivers that go through the
///          arbiter (via \c sentinel::cyhal_i2c_bus_transport) target the
///          singleton; drivers that want direct, unmediated access
///          (via \c sentinel::cyhal_i2c_transport) target the raw CYHAL
///          handle. Both styles coexist intentionally.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.1 - Added bus-arbiter task singletons alongside CYHAL handles
///

#ifndef SENTINEL_RESOURCE_HPP
#define SENTINEL_RESOURCE_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_result.h"
#include "cycfg_peripherals.h"
#include "cycfg_pins.h"
#include "cyhal_gpio.h"
#include "cyhal_hw_types.h"
}
#pragma GCC diagnostic pop

#include "sentinel_task_i2c_bus.hpp"

namespace sentinel::resource {

inline cyhal_pwm_t led1;
inline cyhal_pwm_t led2;
inline cyhal_pwm_t led3;

inline cyhal_i2c_t cybsp_i2c;

///
/// \brief Bus-arbiter task singleton for \ref cybsp_i2c.
///
/// \details One FreeRTOS task owns the underlying CYHAL handle and
///          serialises access for every requester that goes through
///          \c sentinel::cyhal_i2c_bus_transport. Construct here (file-
///          scope inline so storage is in BSS) but call
///          \ref cybsp_i2c_bus.task_create() during system init —
///          \ref peripheral_initialize handles that.
///
inline sentinel::task::i2c_bus cybsp_i2c_bus{&cybsp_i2c, "I2C Bus"};

///
/// \brief GPIO pin wired to the DS3231 INT/SQW output.
///
/// \details The DS3231 emits a 1 Hz square wave on this pin, which
///          \ref sentinel::task::rtc_service takes as a falling-edge
///          interrupt to refresh the firmware's notion of the current time
///          once per second. P6[3] (Arduino header J2_4 / J6_4) is a free
///          GPIO on the same port and VDDIO domain as the I²C bus, so it
///          shares the DS3231's voltage domain and lands on the otherwise-
///          idle port-6 GPIO interrupt vector.
///
///          The pin is owned by Device Configurator (alias \c CYBSP_RTC_SQW:
///          input, pull-up). \c cybsp_init initializes and pin-reserves it,
///          so the rtc_service task must NOT call \c cyhal_gpio_init on it —
///          that would fail with an in-use error. The task only registers a
///          callback and enables the falling-edge event.
///
inline constexpr cyhal_gpio_t rtc_sqw_pin = CYBSP_RTC_SQW;

///
/// \brief Initialize peripheral resources from Device Configurator.
///
/// \details Initializes CYHAL handles from the Device Configurator
///          generated configurations, then spawns the bus-arbiter task
///          so any subsequently-created driver/test task can begin
///          submitting I²C requests immediately.
///
inline void peripheral_initialize() noexcept {
    cyhal_pwm_init_cfg(&led1, &LED1_PWM_hal_config);
    cyhal_pwm_init_cfg(&led2, &LED2_PWM_hal_config);
    cyhal_pwm_init_cfg(&led3, &LED3_PWM_hal_config);

    cyhal_i2c_init_cfg(&cybsp_i2c, &CYBSP_I2C_hal_config);

    // Spawn the I²C bus-arbiter task. Failures here are unrecoverable —
    // every driver downstream expects the arbiter to be running.
    auto bus_rc = cybsp_i2c_bus.task_create();
    configASSERT(bus_rc == pdPASS);
}

///
/// \brief Release peripheral resources from Device Configurator.
///
inline void peripheral_deinitialize() noexcept {
    cyhal_i2c_free(&cybsp_i2c);

    cyhal_pwm_free(&led3);
    cyhal_pwm_free(&led2);
    cyhal_pwm_free(&led1);
}

} // namespace sentinel::resource

#endif /* SENTINEL_RESOURCE_HPP */
