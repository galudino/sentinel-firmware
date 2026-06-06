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
/// \version 1.2 - Added CYBSP_SPI handle and cybsp_spi_bus arbiter singleton
///

#ifndef SENTINEL_RESOURCE_HPP
#define SENTINEL_RESOURCE_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_log.h"
#include "cy_result.h"
#include "cycfg_peripherals.h"
#include "cycfg_pins.h"
#include "cyhal_gpio.h"
#include "cyhal_hw_types.h"
}
#pragma GCC diagnostic pop

#include "sentinel_task_i2c_bus.hpp"
#include "sentinel_task_spi_bus.hpp"

namespace sentinel::resource {

inline cyhal_pwm_t led1;
inline cyhal_pwm_t led2;
inline cyhal_pwm_t led3;

inline cyhal_i2c_t cybsp_i2c;

inline cyhal_spi_t cybsp_spi;

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
/// \brief Bus-arbiter task singleton for \ref cybsp_spi.
///
/// \details One FreeRTOS task owns the underlying CYHAL SPI handle and
///          serialises access for every requester that goes through
///          \c sentinel::cyhal_spi_bus_transport. Construct here (file-
///          scope inline so storage is in BSS) but call
///          \ref cybsp_spi_bus.task_create() during system init —
///          \ref peripheral_initialize handles that.
///
inline sentinel::task::spi_bus cybsp_spi_bus{&cybsp_spi, "SPI Bus"};

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
///          generated configurations, then spawns the bus-arbiter tasks
///          so any subsequently-created driver/test task can begin
///          submitting I²C or SPI requests immediately.
///
inline void peripheral_initialize() noexcept {
#ifdef CYBSP_LED1_PWM_HW
    auto led1_config_result =
        cyhal_pwm_init_cfg(&led1, &CYBSP_LED1_PWM_hal_config);
    configASSERT(led1_config_result == CY_RSLT_SUCCESS);
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "LED1 PWM init result: %d\n",
               static_cast<int>(led1_config_result));
#endif /* CYBSP_LED1_PWM_HW */

#ifdef CYBSP_LED2_PWM_HW
    auto led2_config_result =
        cyhal_pwm_init_cfg(&led2, &CYBSP_LED2_PWM_hal_config);
    configASSERT(led2_config_result == CY_RSLT_SUCCESS);
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "LED2 PWM init result: %d\n",
               static_cast<int>(led2_config_result));
#endif /* CYBSP_LED2_PWM_HW */

#ifdef CYBSP_LED3_PWM_HW
    auto led3_config_result =
        cyhal_pwm_init_cfg(&led3, &CYBSP_LED3_PWM_hal_config);
    configASSERT(led3_config_result == CY_RSLT_SUCCESS);
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "LED3 PWM init result: %d\n",
               static_cast<int>(led3_config_result));
#endif /* CYBSP_LED3_PWM_HW */

#ifdef CYBSP_I2C_HW
    auto i2c_config_result =
        cyhal_i2c_init_cfg(&cybsp_i2c, &CYBSP_I2C_hal_config);
    configASSERT(i2c_config_result == CY_RSLT_SUCCESS);
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "I2C init result: %d\n", static_cast<int>(i2c_config_result));
#endif /* CYBSP_I2C_HW */

#ifdef CYBSP_SPI_HW
    auto spi_config_result =
        cyhal_spi_init_cfg(&cybsp_spi, &CYBSP_SPI_hal_config);
    configASSERT(spi_config_result == CY_RSLT_SUCCESS);
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "SPI init result: %d\n", static_cast<int>(spi_config_result));
#endif /* CYBSP_SPI_HW */

#ifdef CYBSP_I2C_HW
    // Spawn the I²C bus-arbiter task. Failures here are
    // unrecoverable — every driver downstream expects the arbiter to
    // be running.
    auto i2c_bus_task_create_return_code = cybsp_i2c_bus.task_create();
    configASSERT(i2c_bus_task_create_return_code == pdPASS);
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "I2C bus task create result passed: %s\n",
               static_cast<int>(i2c_bus_task_create_return_code) ? "true"
                                                                 : "false");
#endif /* CYBSP_I2C_HW */

#ifdef CYBSP_SPI_HW
    // Spawn the SPI bus-arbiter task. Failures here are
    // unrecoverable — every driver downstream expects the arbiter to
    // be running.
    auto spi_bus_task_create_return_code = cybsp_spi_bus.task_create();
    configASSERT(spi_bus_task_create_return_code == pdPASS);
    cy_log_msg(CY_LOG_FACILITY_T::CYLF_DEF, CY_LOG_LEVEL_T::CY_LOG_INFO,
               "SPI bus task create result passed: %s\n",
               static_cast<int>(spi_bus_task_create_return_code) ? "true"
                                                                 : "false");
#endif /* CYBSP_SPI_HW */
}

///
/// \brief Release peripheral resources from Device Configurator.
///
inline void peripheral_deinitialize() noexcept {
#ifdef CYBSP_SPI_HW
    cyhal_spi_free(&cybsp_spi);
#endif /* CYBSP_SPI_HW */

#ifdef CYBSP_I2C_HW
    cyhal_i2c_free(&cybsp_i2c);
#endif /* CYBSP_I2C_HW */

#ifdef CYBSP_LED3_PWM_HW
    cyhal_pwm_free(&led3);
#endif /* CYBSP_LED3_PWM_HW */

#ifdef CYBSP_LED2_PWM_HW
    cyhal_pwm_free(&led2);
#endif /* CYBSP_LED2_PWM_HW */

#ifdef CYBSP_LED1_PWM_HW
    cyhal_pwm_free(&led1);
#endif /* CYBSP_LED1_PWM_HW */
}

} // namespace sentinel::resource

#endif /* SENTINEL_RESOURCE_HPP */
