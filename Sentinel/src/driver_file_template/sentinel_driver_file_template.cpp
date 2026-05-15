///
/// \file    sentinel_driver_file_template.cpp
/// \brief   Hardware driver template implementation
///
/// \details This file provides a template structure for implementing hardware
///          device driver functionality. Add member function implementations
///          and file-private helper functions as needed.
///
/// \date    2024
/// \version 1.0 - Driver template implementation
///

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cycfg_pins.h"
#include "cyhal_gpio.h"
#include "cyhal_i2c.h"
#include "cyhal_pwm.h"
#include "cyhal_spi.h"
#include "cyhal_system.h"
#include "cyhal_uart.h"
}
#pragma GCC diagnostic pop

#include "sentinel_driver_file_template.hpp"
#include "sentinel_resource.hpp"
#include "sentinel_utilities.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <utility>

// TODO: Add member function implementations here
// TODO: Add file-private helper functions here (with doxygen comments for
// static functions)
