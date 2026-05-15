///
/// \file    sentinel_tach_rpm_windowed.hpp
/// \brief   Windowed RPM calculator for tachometer readings
///
/// \details This header provides a class for calculating RPM over a windowed
/// time interval, useful for measuring rotational speed from tachometer pulses.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0 - Initial implementation
///

#ifndef SENTINEL_TACH_RPM_WINDOWED_HPP
#define SENTINEL_TACH_RPM_WINDOWED_HPP

#include "sentinel_tachometer.hpp"
#include <cstdint>

namespace sentinel {

class tach_rpm_windowed;

}

///
/// \brief Windowed RPM calculator
///
/// \details
///  - You call begin_window(start_tick) with a timestamp from your 1 MHz timer.
///  - GPIO edges are counted via handle_edge().
///  - After your measurement interval (e.g., 1000 ms), you call
///    end_window(end_tick).
///  - RPM is computed as:
///       pulses → revolutions (via pulses_per_rev)
///       rev / elapsed_sec → rev/s
///       rev/s * 60 → RPM
///
class sentinel::tach_rpm_windowed
    : public tach_callback_crtp<tach_rpm_windowed> {
public:
    ///
    /// \param ticks_per_second  Timer frequency (e.g., 1'000'000 for 1 MHz)
    /// \param pulses_per_rev    Tach pulses per mechanical revolution
    ///
    explicit tach_rpm_windowed(uint32_t ticks_per_second,
                               uint8_t pulses_per_rev = 2u) noexcept
        : m_ticks_per_sec(ticks_per_second),
          m_pulses_per_rev(pulses_per_rev ? pulses_per_rev : 1u),
          m_start_tick{0}, m_pulse_count{0}, m_rpm{0} {}

    ///
    /// \brief Called from ISR on each tach pulse edge
    ///
    /// \details We only need the *count* of pulses inside the window,
    ///          not per-edge timing, so we just increment m_pulse_count.
    ///
    void on_edge(uint32_t /*tick*/) noexcept { ++m_pulse_count; }

    ///
    /// \brief Begin a new measurement window
    ///
    /// \param start_tick  Current value of the free-running timer
    ///
    void begin_window(uint32_t start_tick) noexcept {
        m_start_tick = start_tick;
        m_pulse_count = 0;
        m_rpm = 0;
    }

    ///
    /// \brief End the measurement window and compute RPM
    ///
    /// \param end_tick  Current value of the free-running timer
    ///
    void end_window(uint32_t end_tick) noexcept {
        const uint32_t delta_ticks = end_tick - m_start_tick;

        if (delta_ticks == 0u || m_pulse_count == 0u) {
            m_rpm = 0;
            return;
        }

        const float elapsed_sec = static_cast<float>(delta_ticks) /
                                  static_cast<float>(m_ticks_per_sec);

        const float revolutions = static_cast<float>(m_pulse_count) /
                                  static_cast<float>(m_pulses_per_rev);

        const float rev_per_sec = revolutions / elapsed_sec;

        m_rpm = static_cast<uint32_t>(rev_per_sec * 60.0f);
    }

    void reset() noexcept { m_start_tick = m_pulse_count = m_rpm = 0; }

    ///
    /// \brief Get last computed RPM
    ///
    uint32_t rpm() const noexcept { return m_rpm; }

    ///
    /// \brief Get the pulse count from the last window
    ///
    uint32_t pulse_count() const noexcept { return m_pulse_count; }

private:
    // Supplied during object construction, by the caller
    uint32_t m_ticks_per_sec;
    uint8_t m_pulses_per_rev;

    uint32_t m_start_tick;  // Assigned in begin_window()
    uint32_t m_pulse_count; // Assigned when an edge event occurs; ISR function
                            // increments this
    uint32_t
        m_rpm; // (pulse_count / pulses_per_rev) / (delta_ticks / ticks_per_sec)
};

#endif /* SENTINEL_TACH_RPM_WINDOWED_HPP */
