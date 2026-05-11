///
/// \file    pwm_signal.hpp
/// \brief   Platform-agnostic PWM signal interface using CRTP
///
/// \details This header provides a platform-independent interface for PWM
///          signal generation using the Curiously Recurring Template Pattern
///          (CRTP). An implementation class (e.g., CYHAL-based) derives from
///          the façade and provides the concrete HAL-backed functionality.
///          The API is minimal: configure duty/frequency, start/stop, adjust
///          duty at runtime.
///
/// \author  galudino
/// \date    2025
/// \version 1.1 - Unified result types, clamped duty, clarified semantics
///

#ifndef PWM_SIGNAL_HPP
#define PWM_SIGNAL_HPP

/// \ingroup transport
/// \brief Platform-agnostic PWM façade (CRTP)
template <typename Implementation>
class pwm_signal {
public:
    ///
    /// \brief Configure duty/frequency (does not start the PWM)
    ///
    /// \details Sets the logical duty cycle (0–100 %) and frequency (Hz).
    ///          Implementations may invert the hardware duty if the output
    ///          is active-low. This is idempotent and does not enable output.
    ///
    /// \param duty_cycle_pct Logical duty cycle [0..100]
    /// \param frequency_hz   PWM frequency in Hertz
    /// \return uint32_t     0 on success
    ///
    uint32_t configure(uint8_t duty_cycle_pct, uint32_t frequency_hz) noexcept {
        return impl().configure(duty_cycle_pct, frequency_hz);
    }

    ///
    /// \brief Start PWM output
    ///
    /// \return uint32_t 0 on success
    ///
    uint32_t start() noexcept { return impl().start(); }

    ///
    /// \brief Stop PWM output
    ///
    /// \return uint32_t 0 on success
    ///
    uint32_t stop() noexcept { return impl().stop(); }

    ///
    /// \brief Update duty/frequency on a running PWM
    ///
    /// \details Frequency may be unchanged by passing the same value as before.
    ///
    /// \param duty_cycle_pct Logical duty cycle [0..100]
    /// \param frequency_hz   PWM frequency in Hertz
    /// \return uint32_t     0 on success
    ///
    uint32_t set_duty_cycle(uint8_t duty_cycle_pct,
                            uint32_t frequency_hz) noexcept {
        return impl().set_duty_cycle(duty_cycle_pct, frequency_hz);
    }

    ///
    /// \brief Delay in milliseconds
    ///
    /// \param milliseconds Delay in milliseconds
    /// \return Implementation-specific status/result code
    ///
    ///
    /// \brief Delay execution
    ///
    /// \param milliseconds Delay duration in milliseconds
    /// \return Implementation-specific status/result code
    ///
    auto delay(uint32_t milliseconds) noexcept {
        return impl().delay(milliseconds);
    }

    ///
    /// \brief Delay in microseconds
    ///
    /// \param milliseconds Delay in microseconds
    /// \return Implementation-specific status/result code
    ///
    ///
    /// \brief Delay execution
    ///
    /// \param microseconds Delay duration in microseconds
    /// \return Implementation-specific status/result code
    ///
    auto delay_us(uint32_t microseconds) noexcept {
        return impl().delay_us(microseconds);
    }

private:
    ///
    /// \brief Get reference to derived implementation
    ///
    /// \return Reference to implementation
    ///
    Implementation &impl() noexcept {
        return static_cast<Implementation &>(*this);
    }

    ///
    /// \brief Get const reference to derived implementation
    ///
    /// \return Const reference to implementation
    ///
    const Implementation &impl() const noexcept {
        return static_cast<const Implementation &>(*this);
    }
};

#endif /* PWM_SIGNAL_HPP */
