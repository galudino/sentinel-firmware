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
#include "cy_result.h"
#include "cycfg_peripherals.h"
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
/// \brief Initialize peripheral resources from Device Configurator.
///
/// \details Initializes CYHAL handles from the Device Configurator
///          generated configurations, then spawns the bus-arbiter tasks
///          so any subsequently-created driver/test task can begin
///          submitting I²C or SPI requests immediately.
///
inline void peripheral_initialize() noexcept {
    cyhal_pwm_init_cfg(&led1, &LED1_PWM_hal_config);
    cyhal_pwm_init_cfg(&led2, &LED2_PWM_hal_config);
    cyhal_pwm_init_cfg(&led3, &LED3_PWM_hal_config);

    cyhal_i2c_init_cfg(&cybsp_i2c, &CYBSP_I2C_hal_config);
#ifdef CYBSP_SPI_HW
    cyhal_spi_init_cfg(&cybsp_spi, &CYBSP_SPI_hal_config);
#endif

    // Spawn the I²C and SPI bus-arbiter tasks. Failures here are
    // unrecoverable — every driver downstream expects the arbiters to
    // be running.
    auto i2c_bus_rc = cybsp_i2c_bus.task_create();
    configASSERT(i2c_bus_rc == pdPASS);

#ifdef CYBSP_SPI_HW
    auto spi_bus_rc = cybsp_spi_bus.task_create();
    configASSERT(spi_bus_rc == pdPASS);
#endif
}

///
/// \brief Release peripheral resources from Device Configurator.
///
inline void peripheral_deinitialize() noexcept {
#ifdef CYBSP_SPI_HW
    cyhal_spi_free(&cybsp_spi);
#endif
    cyhal_i2c_free(&cybsp_i2c);

    cyhal_pwm_free(&led3);
    cyhal_pwm_free(&led2);
    cyhal_pwm_free(&led1);
}

} // namespace sentinel::resource

#endif /* SENTINEL_RESOURCE_HPP */
