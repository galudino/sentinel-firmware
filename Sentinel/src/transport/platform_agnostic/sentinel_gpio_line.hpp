///
/// \file    sentinel_gpio_line.hpp
/// \brief   Platform-agnostic digital output line + delay abstraction
///
/// \details Defines a tiny, zero-virtual interface for driving digital output
///          lines (e.g., CS/DC/RESET) and a delay provider. Concrete platforms
///          (CYHAL, STM32 HAL, etc.) adapt their GPIO & delay via lightweight
///          wrappers.
///
/// \date    2024–2025
/// \version 1.0
///

#ifndef SENTINEL_GPIO_LINE_HPP
#define SENTINEL_GPIO_LINE_HPP

#include <cstddef>
#include <cstdint>
#include <functional>

namespace sentinel {

///
/// \brief Opaque digital output line (no ownership)
///
/// \details Stores an implementation callback and an opaque context pointer.
///          \c active_low is a logical property for polarity handling.
///
struct gpio_line {
    ///
    /// \brief Function pointer type for writing to a GPIO line
    ///
    /// \param ctx Opaque context pointer (platform-specific GPIO handle)
    /// \param level Logical level to write (true = high, false = low)
    ///
    using write_function = std::function<void(void *ctx, bool level)>;

    void *context{nullptr}; ///< Opaque context pointer for GPIO implementation
    write_function write{
        nullptr}; ///< Function pointer for GPIO write operation
    bool active_low{
        true}; ///< Polarity: true if line is active-low, false if active-high

    ///
    /// \brief Check if the digital output line is valid
    ///
    /// \return true if both context and write function are non-null
    ///
    bool valid() const noexcept {
        return (context != nullptr) && (write != nullptr);
    }

    ///
    /// \brief Set the output level of the line
    ///
    /// \param level Logical level to set (true = active, false = inactive)
    ///
    /// \details Calls the write function if the line is valid. The actual
    ///          hardware level depends on the active_low setting and is
    ///          handled by the implementation.
    ///
    void set(bool level) const noexcept {
        if (valid()) {
            write(context, level);
        }
    }
};

///
/// \brief Delay provider (milliseconds)
///
/// \details Platform-agnostic delay/sleep interface using function pointer.
///          Implementations should provide millisecond-granularity delays.
///
struct delay_api {
    ///
    /// \brief Function pointer type for millisecond delay
    ///
    /// \param ms Delay duration in milliseconds
    ///
    using delay_ms_function = std::function<void(uint32_t ms)>;

    delay_ms_function delay_ms{
        nullptr}; ///< Function pointer for delay implementation

    ///
    /// \brief Check if the delay API is valid
    ///
    /// \return true if delay_ms function pointer is non-null
    ///
    bool valid() const noexcept { return delay_ms != nullptr; }

    ///
    /// \brief Sleep for specified milliseconds
    ///
    /// \param v Delay duration in milliseconds
    ///
    /// \details No-op if delay_ms function pointer is null. Otherwise,
    ///          blocks for approximately v milliseconds.
    ///
    void sleep_ms(uint32_t v) const noexcept {
        if (!valid()) {
            return;
        }

        delay_ms(v);
    }
};

} // namespace sentinel

#endif /* SENTINEL_GPIO_LINE_HPP */
