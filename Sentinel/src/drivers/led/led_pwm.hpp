///
/// \file    led_pwm.hpp
/// \brief   LED control wrapper for PWM-driven LEDs
///
/// \details This header provides a wrapper around PWM signal implementations
///          for controlling LEDs. It offers simplified duty cycle control
///          with predefined states (off, blinking, on) commonly used for
///          status indication such as Bluetooth advertising states.
///
/// \author  galudino
/// \date    2025
/// \version 1.0 - LED module interface
///

#ifndef LED_PWM_HPP
#define LED_PWM_HPP

#include "pwm_signal.hpp"
#include "utilities.hpp"

template <typename PWMImplementation>
class led_pwm;

///
/// \brief LED controller using PWM signal
///
/// \details Wraps a PWM signal implementation to provide simplified LED control
///          with predefined duty cycles for common LED states. Particularly
///          useful for status/advertising LEDs that need to indicate different
///          states (off, blinking, on).
///
/// \tparam PWMImplementation Platform-specific PWM implementation deriving from
///                           pwm_signal
///
template <typename PWMImplementation>
class led_pwm : pwm_signal<PWMImplementation> {
public:
    // Require PWM signal implementation (compile-time check)
    static_assert(std::is_base_of<pwm_signal<PWMImplementation>,
                                  PWMImplementation>::value,
                  "PWMImplementation must derive from "
                  "pwm_signal<PWMImplementation>");

    ///
    /// \brief Predefined LED duty cycle states
    ///
    enum class duty_cycle : uint8_t {
        off = 0,       ///< LED off (0% duty cycle)
        blinking = 50, ///< LED blinking (50% duty cycle)
        on = 100       ///< LED fully on (100% duty cycle)
    };

    ///
    /// \brief Construct LED controller with PWM implementation
    ///
    /// \param pwm Reference to PWM signal implementation (non-owning)
    ///
    explicit led_pwm(PWMImplementation &pwm) noexcept : m_pwm(pwm) {}

    ///
    /// \brief Set LED blink/brightness state
    ///
    /// \param duty Predefined duty cycle state (off, blinking, or on)
    /// \return uint32_t 0 on success, error code otherwise
    ///
    /// \details Updates the PWM duty cycle using the predefined frequency
    ///          for LED operation. The blinking state creates a visible
    ///          flashing effect at ADVERTISING_LED_PWM_FREQUENCY.
    ///
    uint32_t set_blink_rate(duty_cycle duty) {
        return m_pwm.set_duty_cycle(util::to_underlying(duty),
                                    ADVERTISING_LED_PWM_FREQUENCY);
    }

    ///
    /// \brief Start PWM output (turn on LED control)
    ///
    /// \return uint32_t 0 on success, error code otherwise
    ///
    uint32_t start() { return m_pwm.start(); }

    ///
    /// \brief Stop PWM output (disable LED control)
    ///
    /// \return uint32_t 0 on success, error code otherwise
    ///
    uint32_t stop() { return m_pwm.stop(); }

    ///
    /// \brief PWM frequency for advertising LED in Hz
    ///
    /// \details Set to 4 Hz to create a visible blink rate when duty cycle
    ///          is 50%. This frequency provides good visual feedback without
    ///          being distracting.
    ///
    static constexpr auto ADVERTISING_LED_PWM_FREQUENCY = uint32_t{4};

private:
    PWMImplementation &m_pwm; ///< Reference to underlying PWM implementation
};

#endif /* LED_PWM_HPP */
