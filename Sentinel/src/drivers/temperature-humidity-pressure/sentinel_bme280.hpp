///
/// \file    sentinel_bme280.hpp
/// \brief   BME280 temperature/pressure/humidity sensor driver (I2C or SPI)
///
/// \details This header provides a complete driver implementation for the
///          Bosch BME280 combined temperature, barometric pressure, and
///          relative humidity sensor. The driver wraps the official Bosch
///          BME280 API and exposes a C++ interface that automatically forwards
///          to whichever transport (I2C or SPI) the caller wires up.
///
///          Architecture:
///          - Single \c sentinel::bme280<Transport> class template,
///            parameterized over a \ref sentinel::byte_transport implementation.
///          - Transport type is constrained at compile time to derive from
///            \c byte_transport<Transport, i2c_tag> or
///            \c byte_transport<Transport, spi_tag>; the wrapper uses
///            \c if \c constexpr on the detected tag to set the right Bosch
///            \c bme280_intf and (for I2C only) the device target address.
///          - \c sentinel::bme280_i2c<T> and \c sentinel::bme280_spi<T> are
///            provided as convenience aliases at the call sites.
///
///          Default configuration (applied during construction):
///          - Power mode: Forced (one-shot measurements)
///          - Pressure oversampling:    16x (\c BME280_OVERSAMPLING_16X)
///          - Temperature oversampling: 16x (\c BME280_OVERSAMPLING_16X)
///          - Humidity oversampling:    16x (\c BME280_OVERSAMPLING_16X)
///          - IIR filter: Coefficient 16 (\c BME280_FILTER_COEFF_16)
///          - Standby time: 0.5 ms (only relevant in normal mode)
///
///          Output units (when \c BME280_DOUBLE_ENABLE is set, the default):
///          - Pressure:    Pa  (pascals)
///          - Temperature: °C  (degrees Celsius)
///          - Humidity:    %RH (relative humidity, 0–100)
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0 - Transport-agnostic Bosch BME280 driver
///

#ifndef SENTINEL_BME280_HPP
#define SENTINEL_BME280_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "bme280.h"
#include "bme280_defs.h"
#include "cy_result.h"
}
#pragma GCC diagnostic pop

#include "sentinel_byte_transport.hpp"
#include "sentinel_utilities.hpp"

#include <cstdint>
#include <type_traits>

namespace sentinel {

template <typename Transport>
class bme280;

///
/// \ingroup transport
/// \brief Convenience alias for an I2C-backed \ref sentinel::bme280 instance
///
/// \details Equivalent to \c sentinel::bme280<I2CTransport>. The alias exists
///          purely for readability at call sites and as a compile-time hint
///          that the caller intends to construct over an I²C bus.
///
/// \tparam I2CTransport Transport implementation deriving from
///                      \c byte_transport<I2CTransport, i2c_tag>.
///
template <typename I2CTransport>
using bme280_i2c = bme280<I2CTransport>;

///
/// \ingroup transport
/// \brief Convenience alias for a SPI-backed \ref sentinel::bme280 instance
///
/// \details Equivalent to \c sentinel::bme280<SPITransport>. The alias exists
///          purely for readability at call sites and as a compile-time hint
///          that the caller intends to construct over an SPI bus.
///
/// \tparam SPITransport Transport implementation deriving from
///                      \c byte_transport<SPITransport, spi_tag>.
///
template <typename SPITransport>
using bme280_spi = bme280<SPITransport>;

} // namespace sentinel

///
/// \brief BME280 temperature/pressure/humidity sensor driver class
///
/// \details Provides a high-level C++ interface to the Bosch BME280 combined
///          environmental sensor. The class wraps every public function in
///          the Bosch \c bme280.h C API as a member function with matching
///          semantics, and adds two convenience helpers:
///          - \ref read_chip_id "read_chip_id()"
///          - \ref read_temperature_pressure_humidity
///                 "read_temperature_pressure_humidity()"
///
///          Transport selection happens at compile time. The class derives
///          its protocol behavior from the tag of the supplied
///          \ref sentinel::byte_transport specialization:
///          - \ref sentinel::i2c_tag : \c BME280_I2C_INTF is selected, the
///            7-bit target address is programmed onto the bus, and Bosch
///            read/write callbacks issue \c [reg_addr, data...] transactions.
///          - \ref sentinel::spi_tag : \c BME280_SPI_INTF is selected, the
///            target address argument is ignored, and Bosch read/write
///            callbacks set the address MSB to select direction.
///
///          The Bosch C driver handles factory calibration coefficient
///          loading during \c bme280_init() and applies temperature-
///          compensated pressure and humidity formulas in
///          \c bme280_get_sensor_data() / \c bme280_compensate_data().
///
/// \tparam Transport Transport implementation, which must derive from either
///                   \c byte_transport<Transport, i2c_tag> or
///                   \c byte_transport<Transport, spi_tag>.
///
template <typename Transport>
class sentinel::bme280 {
    // ---------------------------------------------------------------------
    // Compile-time tag detection
    // ---------------------------------------------------------------------

    ///
    /// \brief \c true when \c Transport derives from
    ///        \c byte_transport<Transport, i2c_tag>.
    ///
    static constexpr bool is_i2c =
        std::is_base_of_v<byte_transport<Transport, i2c_tag>, Transport>;

    ///
    /// \brief \c true when \c Transport derives from
    ///        \c byte_transport<Transport, spi_tag>.
    ///
    static constexpr bool is_spi =
        std::is_base_of_v<byte_transport<Transport, spi_tag>, Transport>;

    static_assert(is_i2c || is_spi,
                  "Transport must derive from "
                  "sentinel::byte_transport<Transport, i2c_tag> or "
                  "sentinel::byte_transport<Transport, spi_tag>");

public:
    // =====================================================================
    // Construction
    // =====================================================================

    ///
    /// \brief Construct BME280 driver and initialize sensor
    ///
    /// \details Wires the Bosch \c bme280_dev structure to this transport's
    ///          \c bosch_read / \c bosch_write / \c bosch_delay statics,
    ///          performs \c bme280_init() to read factory calibration, issues
    ///          a soft reset, and applies the default configuration described
    ///          in the file header.
    ///
    /// \param bus Reference to the transport instance (non-owning). Must
    ///            outlive this driver.
    /// \param device_address I²C target address. Defaults to
    ///                       \c BME280_I2C_ADDR_PRIM (0x76). Ignored when
    ///                       \c Transport is an SPI transport.
    ///
    /// \note Construction is silent on failure: if any of \c bme280_init,
    ///       \c soft_reset, or the initial \c set_sensor_settings call
    ///       returns a non-zero result code, construction proceeds but
    ///       subsequent member calls will reflect the sensor's unconfigured
    ///       state. Callers wishing to react to initialization failures
    ///       should follow construction with a \ref read_chip_id call.
    ///
    explicit bme280(Transport &bus,
                    uint16_t device_address = BME280_I2C_ADDR_PRIM) noexcept
        : m_bus(bus) {

        if constexpr (is_i2c) {
            m_bus.set_target_address(device_address);
            m_device.intf = BME280_I2C_INTF;
        } else {
            sentinel::unused(device_address);
            m_device.intf = BME280_SPI_INTF;
        }

        m_device.read     = Transport::bosch_read;
        m_device.write    = Transport::bosch_write;
        m_device.delay_us = Transport::bosch_delay;
        m_device.intf_ptr = &bus;

        // Initialize sensor (loads calibration coefficients into m_device).
        auto result = init();
        if (result != BME280_OK) {
            return;
        }

        // Bring the part to a known state before applying defaults.
        result = soft_reset();
        if (result != BME280_OK) {
            return;
        }

        configure_sensor();
    }

    /// Non-copyable: holds a non-owning reference and a mutable Bosch device
    /// structure containing per-instance calibration data, neither of which
    /// can be meaningfully copied.
    bme280(const bme280 &) = delete;
    bme280 &operator=(const bme280 &) = delete;

    /// Movable.
    bme280(bme280 &&) noexcept = default;
    bme280 &operator=(bme280 &&) noexcept = default;

    // =====================================================================
    // Addressing (I²C-only convenience)
    // =====================================================================

    ///
    /// \brief Change I²C target address at runtime (I²C transports only)
    ///
    /// \param address New 7-bit I²C address — typically
    ///                \c BME280_I2C_ADDR_PRIM (0x76) or
    ///                \c BME280_I2C_ADDR_SEC (0x77).
    /// \return Implementation-specific status code from the underlying
    ///         transport. For SPI transports this is a no-op that returns
    ///         \c CY_RSLT_SUCCESS.
    ///
    /// \details The BME280 supports two factory I²C addresses, selected by
    ///          the SDO pin (GND → primary, VDDIO → secondary). Use this to
    ///          retarget when multiple BME280s share a bus.
    ///
    auto set_target_address(uint16_t address) noexcept {
        if constexpr (is_i2c) {
            return m_bus.set_target_address(address);
        } else {
            sentinel::unused(address);
            return CY_RSLT_SUCCESS;
        }
    }

    // =====================================================================
    // Bosch C API — 1:1 member function wrappers
    // =====================================================================

    ///
    /// \brief Write data to sensor registers
    ///
    /// \details Wrapper for \c bme280_set_regs(). Performs a multi-byte
    ///          register write, intended for advanced configuration paths
    ///          that the higher-level wrappers in this class do not cover.
    ///
    /// \param[in] reg_addr Pointer to register addresses to write
    /// \param[in] reg_data Pointer to data buffer
    /// \param      len     Number of bytes to write
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    int8_t set_regs(uint8_t *reg_addr, const uint8_t *reg_data,
                    uint32_t len) const noexcept {
        return bme280_set_regs(reg_addr, reg_data, len, &m_device);
    }

    ///
    /// \brief Read data from sensor registers
    ///
    /// \details Wrapper for \c bme280_get_regs(). Performs a multi-byte
    ///          register read.
    ///
    /// \param      reg_addr  Starting register address
    /// \param[out] reg_data  Pointer to buffer for read data
    /// \param      len       Number of bytes to read
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    int8_t get_regs(uint8_t reg_addr, uint8_t *reg_data,
                    uint32_t len) const noexcept {
        return bme280_get_regs(reg_addr, reg_data, len, &m_device);
    }

    ///
    /// \brief Apply sensor configuration parameters
    ///
    /// \details Wrapper for \c bme280_set_sensor_settings(). \p desired_settings
    ///          is a bitmask of \c BME280_SEL_* flags selecting which fields
    ///          of \p settings should actually be written to the device.
    ///
    /// \param desired_settings Bitmask of \c BME280_SEL_OSR_PRESS,
    ///                         \c BME280_SEL_OSR_TEMP, \c BME280_SEL_OSR_HUM,
    ///                         \c BME280_SEL_FILTER, \c BME280_SEL_STANDBY,
    ///                         or \c BME280_SEL_ALL_SETTINGS.
    /// \param settings         Configuration to apply.
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    int8_t set_sensor_settings(uint8_t desired_settings,
                               const bme280_settings &settings) const noexcept {
        return bme280_set_sensor_settings(desired_settings, &settings,
                                          &m_device);
    }

    ///
    /// \brief Read current sensor configuration
    ///
    /// \details Wrapper for \c bme280_get_sensor_settings().
    ///
    /// \param[out] settings Reference to receive the current configuration.
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    int8_t get_sensor_settings(bme280_settings &settings) const noexcept {
        return bme280_get_sensor_settings(&settings, &m_device);
    }

    ///
    /// \brief Set sensor power mode
    ///
    /// \details Wrapper for \c bme280_set_sensor_mode().
    ///
    /// \param sensor_mode One of:
    ///                    - \c BME280_POWERMODE_SLEEP  (0)
    ///                    - \c BME280_POWERMODE_FORCED (1) — one-shot
    ///                    - \c BME280_POWERMODE_NORMAL (3) — continuous
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    int8_t set_sensor_mode(uint8_t sensor_mode) const noexcept {
        return bme280_set_sensor_mode(sensor_mode, &m_device);
    }

    ///
    /// \brief Get current sensor power mode
    ///
    /// \details Wrapper for \c bme280_get_sensor_mode().
    ///
    /// \param[out] sensor_mode Reference to receive the current power mode
    ///                         (\c BME280_POWERMODE_SLEEP,
    ///                          \c BME280_POWERMODE_FORCED, or
    ///                          \c BME280_POWERMODE_NORMAL).
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    int8_t get_sensor_mode(uint8_t &sensor_mode) const noexcept {
        return bme280_get_sensor_mode(&sensor_mode, &m_device);
    }

    ///
    /// \brief Perform sensor soft reset
    ///
    /// \details Wrapper for \c bme280_soft_reset(). Writes the soft-reset
    ///          command to register \c 0xE0, returning the sensor to its
    ///          power-on configuration. Calibration coefficients are
    ///          preserved in this driver's cached \c bme280_dev.
    ///
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    int8_t soft_reset() const noexcept {
        return bme280_soft_reset(&m_device);
    }

    ///
    /// \brief Read compensated sensor data
    ///
    /// \details Wrapper for \c bme280_get_sensor_data(). Reads the selected
    ///          raw measurements from the sensor's burst-read window and
    ///          applies compensation using the cached calibration data.
    ///
    /// \param sensor_comp Bitmask selecting which channels to read and
    ///                    compensate:
    ///                    - \c BME280_PRESS (0x01)
    ///                    - \c BME280_TEMP  (0x02)
    ///                    - \c BME280_HUM   (0x04)
    ///                    - \c BME280_ALL   (0x07)
    /// \param[out] data   Reference to receive compensated measurements.
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    int8_t get_sensor_data(uint8_t sensor_comp,
                           bme280_data &data) const noexcept {
        return bme280_get_sensor_data(sensor_comp, &data, &m_device);
    }

    ///
    /// \brief Compensate raw sensor data using cached calibration
    ///
    /// \details Wrapper for \c bme280_compensate_data(). Exposed for callers
    ///          that already have raw \c bme280_uncomp_data in hand (for
    ///          example after reading the data registers manually) and want
    ///          to apply Bosch's temperature/pressure/humidity compensation
    ///          formulas without re-issuing bus traffic.
    ///
    /// \param      sensor_comp Bitmask selecting which channels to compensate
    ///                         (\c BME280_PRESS, \c BME280_TEMP, \c BME280_HUM,
    ///                          or \c BME280_ALL).
    /// \param[in]  uncomp_data Raw sensor readings.
    /// \param[out] comp_data   Reference to receive compensated results.
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    int8_t compensate_data(uint8_t sensor_comp,
                           const bme280_uncomp_data &uncomp_data,
                           bme280_data &comp_data) const noexcept {
        return bme280_compensate_data(sensor_comp, &uncomp_data, &comp_data,
                                      &m_device.calib_data);
    }

    ///
    /// \brief Compute maximum measurement delay for a settings combination
    ///
    /// \details Wrapper for \c bme280_cal_meas_delay(). Returns the maximum
    ///          number of microseconds required for one full
    ///          temperature/pressure/humidity measurement at the
    ///          oversampling levels stored in \p settings. Useful in
    ///          forced-mode workflows to size the post-trigger wait.
    ///
    /// \param[out] max_delay Reference to receive the delay in microseconds.
    /// \param      settings  Oversampling configuration to query.
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    /// \note This is a \c static helper because the underlying Bosch
    ///       function does not touch the device handle.
    ///
    static int8_t cal_meas_delay(uint32_t &max_delay,
                                 const bme280_settings &settings) noexcept {
        return bme280_cal_meas_delay(&max_delay, &settings);
    }

    // =====================================================================
    // Convenience helpers
    // =====================================================================

    ///
    /// \brief Read the BME280 chip identifier
    ///
    /// \details Convenience wrapper that reads register \c 0xD0
    ///          (\c BME280_REG_CHIP_ID) into \p id. A working BME280 always
    ///          returns \c 0x60 (\c BME280_CHIP_ID); use this as a presence
    ///          and bus-sanity check after construction.
    ///
    /// \param[out] id Reference to receive the chip ID byte.
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    int8_t read_chip_id(uint8_t &id) const noexcept {
        return get_regs(BME280_REG_CHIP_ID, &id, sizeof(uint8_t));
    }

    ///
    /// \brief One-shot read of temperature, pressure, and humidity
    ///
    /// \details Performs a complete forced-mode measurement cycle:
    ///          1. Triggers a measurement by setting power mode to
    ///             \c BME280_POWERMODE_FORCED.
    ///          2. Queries \c bme280_cal_meas_delay() and waits at least
    ///             that many microseconds for the conversion to complete.
    ///          3. Reads and compensates all three channels via
    ///             \c bme280_get_sensor_data() with \c BME280_ALL.
    ///
    ///          On success, \p output_data holds the compensated readings
    ///          in the units selected by the Bosch driver's compile-time
    ///          option:
    ///          - \c BME280_DOUBLE_ENABLE (default): Pa, °C, %RH as
    ///            \c double values.
    ///          - 32-bit fixed-point fallback: pressure in Pa as
    ///            \c uint32_t, temperature in 1/100 °C as \c int32_t,
    ///            humidity in Q22.10 as \c uint32_t.
    ///
    /// \param[out] output_data Reference to receive compensated readings.
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    /// \note Each call triggers a fresh measurement, so the minimum sensible
    ///       interval between calls is the delay returned by
    ///       \c bme280_cal_meas_delay() (~10 ms at the default 16x
    ///       oversampling configuration).
    ///
    int8_t read_temperature_pressure_humidity(
        bme280_data &output_data) const noexcept {

        // Trigger a single measurement.
        auto result = set_sensor_mode(BME280_POWERMODE_FORCED);
        if (result != BME280_OK) {
            return result;
        }

        // Wait at least the worst-case conversion time, derived from the
        // currently-programmed oversampling settings rather than a hard-
        // coded constant.
        auto settings = bme280_settings{};
        result = get_sensor_settings(settings);
        if (result != BME280_OK) {
            return result;
        }

        auto delay_us_amount = uint32_t{};
        result = cal_meas_delay(delay_us_amount, settings);
        if (result != BME280_OK) {
            return result;
        }

        m_bus.delay_us(delay_us_amount);

        // Read and compensate all three channels.
        return get_sensor_data(BME280_ALL, output_data);
    }

private:
    ///
    /// \brief Initialize the underlying Bosch device handle
    ///
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    /// \details Thin wrapper for \c bme280_init(). Performs the initial
    ///          chip-ID handshake and reads factory calibration into
    ///          \c m_device.calib_data.
    ///
    int8_t init() noexcept { return bme280_init(&m_device); }

    ///
    /// \brief Apply this driver's default sensor configuration
    ///
    /// \details Programs the oversampling, IIR filter, and standby settings
    ///          described in the file header. Called once at the end of
    ///          construction.
    ///
    /// \return \c BME280_OK (0) on success, error code otherwise
    ///
    int8_t configure_sensor() noexcept {
        auto settings = bme280_settings{};

        // Maximum oversampling on all three channels for highest precision.
        settings.osr_p = BME280_OVERSAMPLING_16X;
        settings.osr_t = BME280_OVERSAMPLING_16X;
        settings.osr_h = BME280_OVERSAMPLING_16X;

        // Aggressive IIR filtering smooths out short-term noise without
        // disabling responsiveness.
        settings.filter = BME280_FILTER_COEFF_16;

        // Standby is only consulted in normal mode; pick the shortest value.
        settings.standby_time = BME280_STANDBY_TIME_0_5_MS;

        auto settings_sel = static_cast<uint8_t>(
            BME280_SEL_OSR_PRESS | BME280_SEL_OSR_TEMP | BME280_SEL_OSR_HUM |
            BME280_SEL_FILTER | BME280_SEL_STANDBY);

        return set_sensor_settings(settings_sel, settings);
    }

    Transport &m_bus;             ///< Non-owning reference to the transport.
    mutable bme280_dev m_device{}; ///< Bosch BME280 device handle (mutable
                                   ///< because all Bosch C functions take a
                                   ///< non-const \c bme280_dev*, even on
                                   ///< logically read-only operations).
};

#endif /* SENTINEL_BME280_HPP */
