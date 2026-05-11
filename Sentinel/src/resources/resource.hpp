///
/// \file    resource.hpp
/// \brief   Hardware peripheral resource definitions and initialization
///
/// \details This header provides global peripheral resource handles and
///          initialization functions for UART, SPI, I2C, and PWM peripherals.
///          All resources are defined inline for application-wide access.
///
/// \author  galudino
/// \date    2025
/// \version 1.0 - Peripheral resources
///

#ifndef RESOURCE_HPP
#define RESOURCE_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cy_result.h"
#include "cycfg_peripherals.h"
#include "cyhal_hw_types.h"
}
#pragma GCC diagnostic pop

namespace resource {

inline cyhal_pwm_t led1;
inline cyhal_pwm_t led2;
inline cyhal_pwm_t led3;

///
/// \brief Initialize peripheral resources from Device Configurator.
///
inline void peripheral_initialize() noexcept {
    cyhal_pwm_init_cfg(&led1, &LED1_PWM_hal_config);
    cyhal_pwm_init_cfg(&led2, &LED2_PWM_hal_config);
    cyhal_pwm_init_cfg(&led3, &LED3_PWM_hal_config);
}

///
/// \brief Release peripheral resources from Device Configurator.
///
inline void peripheral_deinitialize() noexcept {
    cyhal_pwm_free(&led3);
    cyhal_pwm_free(&led2);
    cyhal_pwm_free(&led1);
}

} // namespace resource

#endif /* RESOURCE_HPP */
