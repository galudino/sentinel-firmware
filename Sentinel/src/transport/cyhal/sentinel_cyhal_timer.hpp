///
/// \file    sentinel_cyhal_timer.hpp
/// \brief   CYHAL-backed implementation of \ref sentinel::timer
///
/// \details Wraps the Infineon CYHAL timer/counter driver (\c cyhal_timer_*)
///          behind the \ref sentinel::timer<Implementation> CRTP interface,
///          plus a handful of CYHAL-specific extras (event callbacks) not
///          part of the platform-agnostic base.
///

#ifndef SENTINEL_CYHAL_TIMER_HPP
#define SENTINEL_CYHAL_TIMER_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cyhal_timer.h"
}
#pragma GCC diagnostic pop

#include "sentinel_timer.hpp"
#include "sentinel_utilities.hpp"

namespace sentinel {

class cyhal_timer;

}

///
/// \brief CYHAL hardware-timer driver, bound to a \c cyhal_timer_t handle.
///
class sentinel::cyhal_timer : public timer<cyhal_timer> {
public:
    ///
    /// \brief Bind the driver to an already-initialized CYHAL timer object.
    /// \param timer_object Non-owning pointer to a CYHAL timer handle;
    ///                     must outlive this driver instance.
    ///
    explicit cyhal_timer(cyhal_timer_t *timer_object) noexcept
        : m_timer_object(timer_object) {}

    ///
    /// \brief Apply a CYHAL timer configuration.
    /// \param cfg Configuration block (period, direction, compare value, etc.).
    /// \return CYHAL result code (\c CY_RSLT_SUCCESS on success).
    ///
    cy_rslt_t configure(const cyhal_timer_cfg_t *cfg) noexcept {
        return cyhal_timer_configure(m_timer_object, cfg);
    }

    ///
    /// \brief Set the timer's counting frequency.
    /// \param frequency_hz Desired frequency in Hz.
    /// \return CYHAL result code (\c CY_RSLT_SUCCESS on success).
    ///
    cy_rslt_t set_frequency(uint32_t frequency_hz) noexcept {
        return cyhal_timer_set_frequency(m_timer_object, frequency_hz);
    }

    /// \brief Start the timer counting.
    /// \return CYHAL result code (\c CY_RSLT_SUCCESS on success).
    cy_rslt_t start() noexcept { return cyhal_timer_start(m_timer_object); }
    /// \brief Stop the timer.
    /// \return CYHAL result code (\c CY_RSLT_SUCCESS on success).
    cy_rslt_t stop() noexcept { return cyhal_timer_stop(m_timer_object); }
    /// \brief Reset the timer's count value.
    /// \return CYHAL result code (\c CY_RSLT_SUCCESS on success).
    cy_rslt_t reset() noexcept { return cyhal_timer_reset(m_timer_object); }
    /// \brief Read the timer's current count value.
    /// \return The current count value, cast to \c cy_rslt_t by CYHAL.
    cy_rslt_t read() noexcept { return cyhal_timer_read(m_timer_object); }

    ///
    /// \brief Register the ISR-context callback for timer events.
    /// \param callback     Function invoked from interrupt context on the
    ///                     enabled event(s).
    /// \param callback_arg Opaque argument passed back to \p callback.
    ///
    void register_callback(cyhal_timer_event_callback_t callback,
                           void *callback_arg) noexcept {
        cyhal_timer_register_callback(m_timer_object, callback, callback_arg);
    }

    ///
    /// \brief Enable or disable a specific timer interrupt event.
    /// \param event         Event type to configure (e.g. terminal count).
    /// \param intr_priority NVIC priority for the event's interrupt.
    /// \param enable        \c true to enable the event, \c false to disable.
    ///
    void enable_event(cyhal_timer_event_t event, uint8_t intr_priority,
                      bool enable) noexcept {
        cyhal_timer_enable_event(m_timer_object, event, intr_priority, enable);
    }

private:
    cyhal_timer_t *m_timer_object; ///< Non-owning CYHAL timer handle.
};

#endif /* SENTINEL_CYHAL_TIMER_HPP */
