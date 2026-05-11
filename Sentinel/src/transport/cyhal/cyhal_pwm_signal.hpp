///
/// \file    cyhal_pwm_signal.hpp
/// \brief   CYHAL (Cypress HAL) PWM signal implementation
///
/// \details Implements the \ref pwm_signal façade over a pre-initialized
///          CYHAL PWM object. Supports active-high/active-low outputs by
///          inverting the hardware duty cycle as needed. Includes convenience
///          helpers for perceptual brightness and timed fades.
///
/// \example
/// \code
/// Construct from a pre-initialized CYHAL PWM object
/// cyhal_pwm_signal backlight(&front_yellow_led_object,
/// /*active_low=*/true);
///
/// // Program 1 kHz, 50% logical duty (inverted in HW if active_low)
/// backlight.configure(50, 1000);
///
/// // Start output
/// backlight.start();
///
/// // Later, dim to 20%
/// backlight.set_duty_cycle(20, backlight.frequency());
///
/// // Stop output
/// backlight.stop();
/// \endcode
///
/// \example
/// \code
/// cyhal_pwm_signal backlight(&pwm_obj, /*active_low=*/true);
///
/// Set perceptual brightness to 30% with gamma 2.0
/// backlight.set_brightness_0_1(0.30f, 2.0f);
///
/// Change frequency only (keep brightness)
/// backlight.set_frequency(2000);
///
/// Smoothly fade to 80% logical duty over 300 ms
/// backlight.fade_to(80, 300, 5);
/// \endcode
///
/// \author  galudino
/// \date    2025
/// \version 1.2 - Added set_frequency, set_brightness_0_1, fade_to
///

#ifndef CYHAL_PWM_SIGNAL_HPP
#define CYHAL_PWM_SIGNAL_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cyhal_hw_types.h"
#include "cyhal_pwm.h"
#include "cyhal_system.h" // for cyhal_system_delay_ms
}
#pragma GCC diagnostic pop

#include "pwm_signal.hpp"

#include <cmath> // powf, roundf

/// \ingroup transport
/// \brief CYHAL-based PWM signal implementation
class cyhal_pwm_signal : public pwm_signal<cyhal_pwm_signal> {
public:
    ///
    /// \brief Construct with an initialized CYHAL PWM object
    ///
    /// \param pwm_object Pointer to a valid, initialized CYHAL PWM object
    /// \param active_low If true, logical duty is inverted for hardware output
    ///
    explicit cyhal_pwm_signal(cyhal_pwm_t *pwm_object,
                              bool active_low = true) noexcept
        : m_pwm_object(pwm_object), m_active_low(active_low) {}

    ///
    /// \brief Configure duty/frequency (does not start the PWM)
    ///
    /// \param duty_cycle_pct Logical duty [0..100]
    /// \param frequency_hz   Frequency in Hertz
    /// \return cy_rslt_t
    ///
    cy_rslt_t configure(uint8_t duty_cycle_pct,
                        uint32_t frequency_hz) noexcept {
        // program registers; do not start implicitly
        return set_duty_cycle(duty_cycle_pct, frequency_hz);
    }

    ///
    /// \brief Start PWM output
    ///
    cy_rslt_t start() noexcept { return cyhal_pwm_start(m_pwm_object); }

    ///
    /// \brief Stop PWM output
    ///
    cy_rslt_t stop() noexcept { return cyhal_pwm_stop(m_pwm_object); }

    ///
    /// \brief Update duty/frequency on a running (or stopped) PWM
    ///
    /// \param duty_cycle_pct Logical duty [0..100]
    /// \param frequency_hz   Frequency in Hertz
    /// \return cy_rslt_t
    ///
    cy_rslt_t set_duty_cycle(uint8_t duty_cycle_pct,
                             uint32_t frequency_hz) noexcept {
        const auto hw_duty =
            static_cast<float>(hardware_duty_cycle(clamp_pct(duty_cycle_pct)));
        const auto hw_freq = static_cast<float>(frequency_hz);

        auto result = cyhal_pwm_set_duty_cycle(m_pwm_object, hw_duty, hw_freq);

        if (result == CY_RSLT_SUCCESS) {
            m_last_duty = clamp_pct(duty_cycle_pct);
            m_last_freq = frequency_hz;
        }

        return result;
    }

    ///
    /// \brief Change frequency, preserving current logical duty
    ///
    /// \param frequency_hz New frequency (Hz)
    /// \return cy_rslt_t
    ///
    cy_rslt_t set_frequency(uint32_t frequency_hz) noexcept {
        return set_duty_cycle(m_last_duty, frequency_hz);
    }

    ///
    /// \brief Set brightness in [0,1] with gamma correction (default 2.2)
    ///
    /// \details Maps perceptual brightness \p x to logical duty percent using:
    ///          \f$ duty = \mathrm{round}(100 \cdot x^{1/\gamma}) \f$.
    ///          The duty is then inverted in hardware if \c active_low is true.
    ///
    /// \param x      Brightness in [0,1]
    /// \param gamma  Gamma exponent (> 0), default 2.2
    /// \return cy_rslt_t
    ///
    cy_rslt_t set_brightness_0_1(float x, float gamma = 2.2f) noexcept {
        if (!(gamma > 0.0f)) {
            gamma = 2.2f;
        }

        if (x < 0.0f) {
            x = 0.0f;
        }

        if (x > 1.0f) {
            x = 1.0f;
        }

        const auto lin = std::pow(x, 1.0f / gamma);
        const auto duty_pct = static_cast<uint8_t>(std::lround(100.0f * lin));

        return set_duty_cycle(duty_pct, m_last_freq);
    }

    ///
    /// \brief Blockingly fade to a new logical duty over time
    ///
    /// \details Performs a linear ramp from the current logical duty to
    ///          \p duty_target in \p duration_ms, stepping every \p step_ms.
    ///          Uses \c cyhal_system_delay_ms for timing.
    ///
    /// \param duty_target  Target logical duty [0..100]
    /// \param duration_ms  Total fade duration in milliseconds
    /// \param step_ms      Step interval in milliseconds (default 5 ms)
    /// \return cy_rslt_t   First error encountered, or CY_RSLT_SUCCESS
    ///
    cy_rslt_t fade_to(uint8_t duty_target, uint32_t duration_ms,
                      uint32_t step_ms = 5) noexcept {
        duty_target = clamp_pct(duty_target);

        if (duration_ms == 0 || step_ms == 0 || duty_target == m_last_duty) {
            // trivial case: set directly
            return set_duty_cycle(duty_target, m_last_freq);
        }

        auto steps = duration_ms / step_ms;

        if (steps == 0) {
            steps = 1;
        }

        const auto start = static_cast<int>(m_last_duty);
        const auto end = static_cast<int>(duty_target);
        const auto delta = end - start;

        auto status = CY_RSLT_SUCCESS;

        for (auto i = uint32_t{1}; i <= steps; i++) {
            // linear interpolation in logical duty space
            const auto t = static_cast<float>(i) / static_cast<float>(steps);
            const auto cur = start + static_cast<int>(std::lround(t * delta));

            status = set_duty_cycle(static_cast<uint8_t>(cur), m_last_freq);

            if (status != CY_RSLT_SUCCESS) {
                break;
            }

            if (i < steps) {
                delay(step_ms);
            }
        }
        return status;
    }

    // ---------------------------------------------------------------------
    // Introspection / controls
    // ---------------------------------------------------------------------

    ///
    /// \brief Get current logical duty cycle percentage
    ///
    /// \return Current duty cycle in range [0..100]
    ///
    uint8_t duty() const noexcept { return m_last_duty; }

    ///
    /// \brief Get current PWM frequency
    ///
    /// \return Current frequency in Hertz
    ///
    uint32_t frequency() const noexcept { return m_last_freq; }

    ///
    /// \brief Set active-low polarity mode
    ///
    /// \param low true for active-low (inverts duty), false for active-high
    ///
    /// \details When active-low is enabled, the hardware duty cycle is
    /// inverted.
    ///          For example, logical 20% becomes hardware 80%.
    ///
    void set_active_low(bool low) noexcept { m_active_low = low; }

    ///
    /// \brief Get active-low polarity mode
    ///
    /// \return true if active-low mode is enabled, false otherwise
    ///
    bool active_low() const noexcept { return m_active_low; }

    ///
    /// \brief Delay
    ///
    /// \param milliseconds Delay in milliseconds
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t delay(uint32_t milliseconds) noexcept {
        return cyhal_system_delay_ms(milliseconds);
    }

    ///
    /// \brief Delay
    ///
    /// \param microseconds Delay in microseconds
    /// \return cy_rslt_t CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t delay_us(uint32_t microseconds) noexcept {
        cyhal_system_delay_us(microseconds);
        return CY_RSLT_SUCCESS;
    }

private:
    ///
    /// \brief Clamp duty cycle percentage to valid range
    ///
    /// \param p Duty cycle percentage (may be > 100)
    /// \return Clamped value in range [0..100]
    ///
    static constexpr uint8_t clamp_pct(uint8_t p) noexcept {
        return (p > 100u) ? 100u : p;
    }

    ///
    /// \brief Convert logical duty to hardware duty
    ///
    /// \param logical_pct Logical duty cycle percentage [0..100]
    /// \return Hardware duty cycle (inverted if active_low is true)
    ///
    /// \details If active_low is true, returns (100 - logical_pct).
    ///          Otherwise returns logical_pct unchanged.
    ///
    uint8_t hardware_duty_cycle(uint8_t logical_pct) const noexcept {
        return m_active_low ? static_cast<uint8_t>(100u - logical_pct)
                            : logical_pct;
    }

    cyhal_pwm_t *m_pwm_object; ///< Pointer to CYHAL PWM object
    bool m_active_low;         ///< true if output is active-low

    // Cached logical settings (not inverted)
    uint8_t m_last_duty{0};  ///< Last configured logical duty cycle [0..100]
    uint32_t m_last_freq{0}; ///< Last configured frequency in Hz
};

#endif /* CYHAL_PWM_SIGNAL_HPP */
