///
/// \file    sentinel_cyhal_gpio_line.hpp
/// \brief   CYHAL adapter for sentinel::gpio_line
///
/// \details Bridges CYHAL GPIO into Sentinel's
///          platform-agnostic GPIO line abstraction.
///
/// \date    2024–2025
/// \version 1.0
///

#ifndef SENTINEL_CYHAL_GPIO_LINE_HPP
#define SENTINEL_CYHAL_GPIO_LINE_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cyhal_gpio.h"
#include "cyhal_system.h"
}
#pragma GCC diagnostic pop

#include "sentinel_gpio_line.hpp"

#include <cstdint>

namespace sentinel {

///
/// \brief Context structure for CYHAL GPIO line adapter
///
/// \details Stores GPIO pin configuration and polarity information for
///          use with the sentinel::gpio_line abstraction. The context must
///          remain valid for the lifetime of the associated sentinel::gpio_line.
///
struct cyhal_gpio_line_context {
    cyhal_gpio_t pin{
        cyhal_gpio_psoc6_01_116_bga_ble_t::NC}; ///< CYHAL GPIO pin identifier
    bool active_low{true}; ///< true if pin is active-low, false if active-high
};

///
/// \brief Write to a CYHAL GPIO pin with polarity handling
///
/// \details Callback function used by sentinel::gpio_line to write to a GPIO pin.
///          Automatically handles active-low/active-high polarity conversion.
///
/// \param ctx Opaque context pointer (must be cyhal_gpio_line *)
/// \param logical_level Logical level to write (true = active, false =
/// inactive)
///
/// \note The hardware level is inverted if active_low is true in the context.
///       No-op if context is null or pin is NC (not connected).
///
inline void cyhal_gpio_line_write(void *ctx, bool logical_level) {
    auto *context = static_cast<cyhal_gpio_line_context *>(ctx);
    if (context == nullptr ||
        context->pin == cyhal_gpio_psoc6_01_116_bga_ble_t::NC) {
        return;
    }

    // logical true -> drive high unless active_low dictates inversion
    auto hardware_level = context->active_low ? !logical_level : logical_level;
    cyhal_gpio_write(context->pin, hardware_level);
}

///
/// \brief Create an sentinel::gpio_line bound to a CYHAL GPIO pin
///
/// \details Constructs a platform-agnostic sentinel::gpio_line that wraps a
///          CYHAL GPIO pin. The resulting line can be used with any code
///          expecting the sentinel::gpio_line interface.
///
/// \param context Reference to persistent context (must outlive the line)
///
/// \return sentinel::gpio_line configured for the specified CYHAL pin
///
/// \note The context must remain valid for the lifetime of the returned line.
///       The line does not take ownership of the context.
///
inline gpio_line make_cyhal_gpio_line(cyhal_gpio_line_context &context) {
    auto line = gpio_line{};
    line.context = &context;
    line.write = &cyhal_gpio_line_write;
    line.active_low = context.active_low; // informational
    return line;
}

} // namespace sentinel

#endif /* SENTINEL_CYHAL_GPIO_LINE_HPP */
