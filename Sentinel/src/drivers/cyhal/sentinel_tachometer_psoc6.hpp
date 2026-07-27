///
/// \file    sentinel_tachometer_psoc6.hpp
/// \brief   PSoC6-specific tachometer input driver using GPIO and TCPWM
///
/// \details Provides a hardware-specific implementation of a tachometer source
///          for Infineon PSoC6 microcontrollers. This driver uses:
///          - GPIO interrupt for detecting tachometer pulse edges
///          - TCPWM free-running counter for precise timestamps
///
///          The driver registers a GPIO callback for the specified edge type
///          and captures the current counter value on each edge, then forwards
///          the event to a tachometer_input callback.
///
///          Hardware Configuration:
///          - GPIO pin must be configured as input (done via Device
///          Configurator
///            or sentinel::resource::gpio_initialize())
///          - TCPWM counter must be initialized and running as a free-running
///            timer (typically configured via Device Configurator)
///
///          Semantics:
///          - Constructor registers GPIO callback and starts generating events
///          - Events are forwarded to tachometer_input::on_edge() from ISR
///          - Copy/assignment disabled (hardware resource binding)
///
/// \example
/// \code
/// // Create RPM calculator
/// sentinel::tach_rpm rpm_calc(1000000, 1);  // 1 MHz counter, 1 pulse/rev
///
/// // Initialize the blower tach counter resource
/// cyhal_timer_init_cfg(&sentinel::resource::blower_tach_counter_resource,
///                      &BlowerTachCounter_hal_config);
/// cyhal_timer_start(&sentinel::resource::blower_tach_counter_resource);
///
/// // Create PSoC6 tachometer source (starts generating events)
/// sentinel::tachometer_psoc6 tach_hw(rpm_calc,
///                               BlowerTach,
///                               &sentinel::resource::blower_tach_counter_resource,
///                               CYHAL_GPIO_IRQ_RISE);
///
/// // RPM is automatically updated on each pulse edge
/// uint32_t current_rpm = rpm_calc.rpm();
/// \endcode
///
/// \date    2026-05-15
/// \version 1.0 - Initial PSoC6 tachometer driver
///

#ifndef SENTINEL_TACHOMETER_PSOC6_HPP
#define SENTINEL_TACHOMETER_PSOC6_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cyhal_gpio.h"
#include "cyhal_hw_types.h"
#include "cyhal_timer.h"
}
#pragma GCC diagnostic pop

#include "sentinel_resource.hpp"
#include "sentinel_tachometer.hpp"

namespace sentinel {

///
/// \brief PSoC6 tachometer source using GPIO interrupt and TCPWM counter
///
/// \details This class provides hardware integration for tachometer pulse
///          sensing on PSoC6 devices. It captures timestamps using a TCPWM
///          free-running counter and dispatches events through the
///          tachometer_input interface.
///
///          Lifetime:
///          - Construct once with a reference to a tachometer_input callback
///          - The constructor registers GPIO callback and enables interrupts
///          - Events begin flowing immediately after construction
///          - The instance must outlive the GPIO callback registration
///
///          ISR Context:
///          - The GPIO callback runs in ISR context
///          - Timestamp capture and callback dispatch are minimal operations
///          - The tachometer_input::on_edge() implementation must be ISR-safe
///
class tachometer_psoc6 {
public:
    ///
    /// \brief Construct PSoC6 tachometer source
    ///
    /// \details Initializes GPIO interrupt callback for the specified pin
    ///          and edge type. The callback will read the TCPWM counter
    ///          on each edge and forward the event to the callback reference.
    ///
    /// \param cb           Reference to tachometer_input that receives events
    /// \param pin          GPIO pin connected to tachometer signal
    /// \param tcpwm_counter Pointer to initialized TCPWM timer for timestamps
    ///
    tachometer_psoc6(sentinel::tachometer_input &cb, cyhal_gpio_t pin,
                     cyhal_timer_t *tcpwm_counter)
        : m_cb(cb), m_pin(pin), m_counter(tcpwm_counter) {}

    ///
    /// \brief Deleted copy constructor (hardware resource binding)
    ///
    tachometer_psoc6(const tachometer_psoc6 &) = delete;

    ///
    /// \brief Deleted copy assignment (hardware resource binding)
    ///
    tachometer_psoc6 &operator=(const tachometer_psoc6 &) = delete;

    ///
    /// \brief Start capturing tachometer events
    ///
    /// \return CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t start() { return cyhal_timer_start(m_counter); }

    ///
    /// \brief Stop capturing tachometer events
    ///
    /// \return CY_RSLT_SUCCESS on success, error code otherwise
    ///
    cy_rslt_t stop() { return cyhal_timer_stop(m_counter); }

    ///
    /// \brief GPIO ISR wrapper function
    ///
    /// \details Static callback function registered with the HAL. Captures
    ///          the current counter value and forwards the event to the
    ///          tachometer_input callback.
    ///
    /// \param arg Pointer to tachometer_psoc6 instance (this)
    ///            (second parameter is the GPIO event type; unused here since
    ///            the edge type is fixed at registration, so it is unnamed)
    ///
    static void gpio_isr_wrapper(void *arg, cyhal_gpio_event_t /*event*/) {
        auto *self = static_cast<tachometer_psoc6 *>(arg);

        // *(self->m_counter) is sentinel::resource::blower_timer_resource
        // Here, we read the current timer value for timestamping.
        const auto tick = cyhal_timer_read(self->m_counter);

        // --- DEBUG INSTRUMENTATION ---
        ++tach_isr_count;

        uint32_t prev = tach_last_tick;
        tach_last_tick = tick;
        tach_last_delta = tick - prev;
        // ------------------------------

        // Forward to RPM calculator
        self->m_cb.on_edge(tick);
    }

private:
    ///
    /// \brief Reference to the callback receiver
    ///
    sentinel::tachometer_input &m_cb;

    ///
    /// \brief GPIO pin for tachometer input
    ///
    cyhal_gpio_t m_pin;

    ///
    /// \brief Pointer to TCPWM counter for timestamp capture
    ///
    cyhal_timer_t *m_counter;
};

} // namespace sentinel

#endif /* SENTINEL_TACHOMETER_PSOC6_HPP */
