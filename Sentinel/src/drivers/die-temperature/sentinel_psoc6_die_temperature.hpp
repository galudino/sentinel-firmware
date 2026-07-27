///
/// \file    sentinel_psoc6_die_temperature.hpp
/// \brief   PSoC 6 on-die temperature via the SAR ADC DieTemp sensor
///
/// \details Reads the CY8C6xxx (PSoC 63 / CYBLE-416045) internal die
/// temperature
///          through the SAR ADC's DieTemp sensor channel and converts the raw
///          counts to °C using the per-part SFLASH calibration
///          (\c SFLASH->SAR_TEMP_MULTIPLIER / \c SAR_TEMP_OFFSET) with the
///          canonical dual-slope algorithm (the DieTemp component / TRM Ch.
///          39).
///
///          Feeds \c device_snapshot::cpu_temperature_001c (#36/#6). A single
///          conversion (single-ended DieTemp channel, 1.2 V bandgap reference,
///          32× hardware averaging) takes on the order of tens of microseconds;
///          \ref sentinel::drivers::psoc6_die_temperature::refresh throttles to
///          ~1 Hz and serializes SAR access under a
///          mutex, so the two snapshot producers (100 ms stream + 5 min
///          persist) share the SAR safely without re-converting on every
///          populate.
///
///          OO/class singleton (decision #16). Off-bench builds compile this
///          but never touch the SAR — \c populate_snapshot only reads the
///          cache, which is 0/invalid until
///          \ref sentinel::drivers::psoc6_die_temperature::initialize runs on
///          real hardware.
///
///          \note The mV→°C calibration and the chosen SAR clock divider need
///          \b on-bench validation — tracked in issue #55.
///
/// \author  galudino
/// \date    2026-07-08
/// \version 1.0 - Initial PSoC 6 die-temperature driver
///

#ifndef SENTINEL_PSOC6_DIE_TEMPERATURE_HPP
#define SENTINEL_PSOC6_DIE_TEMPERATURE_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <FreeRTOS.h>
#include <semphr.h>
}
#pragma GCC diagnostic pop

#include <cstdint>

namespace sentinel::drivers {

///
/// \brief Single-owner reader for the PSoC 6 SAR-ADC on-die temperature sensor.
///
/// \note This class is non-copyable and non-movable.
///
class psoc6_die_temperature {
public:
    /// \brief The single die-temperature-driver instance.
    /// \return Reference to the process-wide \ref psoc6_die_temperature
    ///         singleton.
    static psoc6_die_temperature &instance() noexcept;

    psoc6_die_temperature(const psoc6_die_temperature &) = delete;
    psoc6_die_temperature &operator=(const psoc6_die_temperature &) = delete;
    psoc6_die_temperature(psoc6_die_temperature &&) = delete;
    psoc6_die_temperature &operator=(psoc6_die_temperature &&) = delete;

    ///
    /// \brief Bring up the AREF + SAR for die-temperature reads.
    ///
    /// \details Enables the analog reference, assigns a SAR clock, and
    /// initializes
    ///          the SAR with the DieTemp-sensor channel configuration.
    ///          Idempotent. Call once post-scheduler (the boot orchestrator
    ///          does).
    ///
    /// \return \c true on success; \c false if the SAR/AREF could not be
    /// brought
    ///         up (the driver then stays inert and \ref cached_centi_c reports
    ///         invalid).
    ///
    bool initialize() noexcept;

    ///
    /// \brief Take a fresh die-temperature reading into the cache (throttled).
    ///
    /// \details A no-op if not \ref initialize d or if called again within the
    ///          ~1 s throttle window. Serializes SAR access under a mutex, so
    ///          it is safe to call from either snapshot producer.
    ///
    void refresh() noexcept;

    ///
    /// \brief Most recent cached die temperature, in 0.01 °C.
    ///
    /// \param[out] out_centi_c Receives the cached temperature (0.01 °C / LSB).
    /// \return \c true if a valid reading has been cached; otherwise \c false
    ///         (and \p out_centi_c is untouched).
    ///
    bool cached_centi_c(int16_t &out_centi_c) const noexcept;

private:
    psoc6_die_temperature() = default;

    /// \brief One synchronous SAR conversion → 0.01 °C (SAR access, no
    /// locking).
    /// \param[out] out_centi_c Receives the freshly converted temperature
    ///             (0.01 °C / LSB).
    /// \return \c true on a successful conversion; \c false otherwise (and
    ///         \p out_centi_c is untouched).
    bool convert_once(int16_t &out_centi_c) noexcept;

    /// \brief SFLASH-calibrated dual-slope counts → 0.01 °C conversion.
    /// \param adc_counts Raw SAR ADC counts from the DieTemp channel.
    /// \return Temperature in 0.01 °C / LSB.
    static int16_t counts_to_centi_c(int16_t adc_counts) noexcept;

    SemaphoreHandle_t m_mutex{nullptr};   ///< Serializes SAR access.
    volatile int16_t m_cached_centi_c{0}; ///< Last reading, 0.01 °C.
    volatile bool m_valid{false};         ///< A reading has been cached.
    bool m_initialized{false};            ///< SAR/AREF brought up.
    uint32_t m_last_refresh_tick{0};      ///< Throttle timestamp (ms).
};

} // namespace sentinel::drivers

#endif /* SENTINEL_PSOC6_DIE_TEMPERATURE_HPP */
