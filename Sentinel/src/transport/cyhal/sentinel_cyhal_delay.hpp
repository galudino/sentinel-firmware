///
/// \file    sentinel_cyhal_delay.hpp
/// \brief   Full-width microsecond delay for the CYHAL transports
///
/// \details \c cyhal_system_delay_us() takes a \c uint16_t, so any request of
///          65 536 µs or more is silently truncated modulo 2^16. The transports
///          expose \c delay_us(uint32_t), and the Bosch BME280 driver uses it
///          to wait for a forced-mode conversion: at 16x oversampling on all
///          three channels that is 112 800 µs, which truncated to 47 264 µs.
///          The driver then read the data registers ~65 ms before the
///          conversion finished and got the \e previous result — on the first
///          read after reset, the chip's reset values, which compensate to the
///          impossible 25.07 °C / 645.6 hPa / 91.3 %RH stamped into every boot
///          snapshot (#89).
///
///          \ref sentinel::cyhal_delay_us honours the full 32-bit request:
///          whole milliseconds go through \c cyhal_system_delay_ms() (an RTOS
///          delay under \c CY_RTOS_AWARE, so a long wait yields the CPU instead
///          of spinning), and the sub-millisecond remainder — or everything,
///          before the scheduler runs — busy-waits in \c uint16_t-sized chunks.
///
/// \author  galudino
/// \date    2026-11-13
/// \version 1.0
///

#ifndef SENTINEL_CYHAL_DELAY_HPP
#define SENTINEL_CYHAL_DELAY_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cyhal_system.h"
#include "task.h"
}
#pragma GCC diagnostic pop

#include <cstdint>

namespace sentinel {

///
/// \brief Delay for at least \p microseconds, without 16-bit truncation.
///
/// \param microseconds Delay duration in microseconds (full 32-bit range).
///
inline void cyhal_delay_us(uint32_t microseconds) noexcept {
    constexpr uint32_t US_PER_MS = 1000u;
    constexpr uint32_t MAX_CHUNK_US = 0xFFFFu; // cyhal_system_delay_us limit

    // Whole milliseconds: yield via the RTOS once it is running (the HAL adds
    // one tick so the wait is never shorter than requested).
    if (microseconds >= US_PER_MS &&
        xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        cyhal_system_delay_ms(microseconds / US_PER_MS);
        microseconds %= US_PER_MS;
    }

    // Remainder (or the whole request pre-scheduler): busy-wait in chunks the
    // uint16_t HAL call can represent.
    while (microseconds > 0u) {
        const uint32_t chunk =
            microseconds > MAX_CHUNK_US ? MAX_CHUNK_US : microseconds;
        cyhal_system_delay_us(static_cast<uint16_t>(chunk));
        microseconds -= chunk;
    }
}

} // namespace sentinel

#endif /* SENTINEL_CYHAL_DELAY_HPP */
