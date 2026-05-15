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

class sentinel::cyhal_timer : public timer<cyhal_timer> {
public:
    explicit cyhal_timer(cyhal_timer_t *timer_object) noexcept
        : m_timer_object(timer_object) {}

    cy_rslt_t configure(const cyhal_timer_cfg_t *cfg) noexcept {
        return cyhal_timer_configure(m_timer_object, cfg);
    }

    cy_rslt_t set_frequency(uint32_t frequency_hz) noexcept {
        return cyhal_timer_set_frequency(m_timer_object, frequency_hz);
    }

    cy_rslt_t start() noexcept { return cyhal_timer_start(m_timer_object); }
    cy_rslt_t stop() noexcept { return cyhal_timer_stop(m_timer_object); }
    cy_rslt_t reset() noexcept { return cyhal_timer_reset(m_timer_object); }
    cy_rslt_t read() noexcept { return cyhal_timer_read(m_timer_object); }

    void register_callback(cyhal_timer_event_callback_t callback,
                           void *callback_arg) noexcept {
        cyhal_timer_register_callback(m_timer_object, callback, callback_arg);
    }

    void enable_event(cyhal_timer_event_t event, uint8_t intr_priority,
                      bool enable) noexcept {
        cyhal_timer_enable_event(m_timer_object, event, intr_priority, enable);
    }

private:
    cyhal_timer_t *m_timer_object;
};

#endif /* SENTINEL_CYHAL_TIMER_HPP */
