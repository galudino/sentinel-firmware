///
/// \file    sentinel_timer.hpp
/// \brief   Platform-agnostic CRTP timer/counter interface
///
/// \details Defines \ref sentinel::timer, a zero-virtual CRTP base that
///          forwards hardware-timer operations to a concrete platform
///          implementation (e.g. \c sentinel::cyhal_timer). Callers program
///          against this interface; the derived class supplies the actual
///          register-level behavior.
///

#ifndef SENTINEL_TIMER_HPP
#define SENTINEL_TIMER_HPP

#include <cstdint>

namespace sentinel {

///
/// \brief CRTP timer/counter facade.
///
/// \tparam Implementation Concrete derived class providing the same member
///                        functions (e.g. \c sentinel::cyhal_timer). Must
///                        publicly derive from \c timer<Implementation>.
///
template <typename Implementation>
class timer {
public:
    ///
    /// \brief Set the timer's counting frequency.
    ///
    /// \param frequency_hz Desired frequency in Hz.
    /// \return Implementation-defined status code from the underlying call.
    ///
    uint32_t set_frequency(uint32_t frequency_hz) noexcept {
        return impl().set_frequency(frequency_hz);
    }

    /// \brief Start the timer counting.
    /// \return Implementation-defined status code from the underlying call.
    uint32_t start() noexcept { return impl().start(); }
    /// \brief Stop the timer.
    /// \return Implementation-defined status code from the underlying call.
    uint32_t stop() noexcept { return impl().stop(); }
    /// \brief Reset the timer's count value.
    /// \return Implementation-defined status code from the underlying call.
    uint32_t reset() noexcept { return impl().reset(); }
    /// \brief Read the timer's current count value.
    /// \return The current count, per the underlying implementation.
    uint32_t read() noexcept { return impl().read(); }

private:
    /// \brief Access this object as its concrete \c Implementation.
    /// \return Reference to \c *this, viewed as \c Implementation.
    Implementation &impl() noexcept {
        return static_cast<Implementation &>(*this);
    }

    /// \brief Access this object as its concrete \c Implementation (const).
    /// \return Const reference to \c *this, viewed as \c Implementation.
    const Implementation &impl() const noexcept {
        return static_cast<const Implementation &>(*this);
    }
};

} // namespace sentinel

#endif /* SENTINEL_TIMER_HPP */
