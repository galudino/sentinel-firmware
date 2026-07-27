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
///            parameterized over a \ref sentinel::byte_transport
///            implementation.
///          - Transport type is constrained at compile time to derive from
///            \c byte_transport<Transport, i2c_tag> or
///            \c byte_transport<Transport, spi_tag>; the wrapper uses
///            \c if \c constexpr on the detected tag to set the right Bosch
///            \c bme280_intf and (for I2C only) the device target address.
///          - \c sentinel::bme280_i2c<T> and \c sentinel::bme280_spi<T> are
///            provided as convenience aliases at the call sites.
///
///          Public API design:
///          - Operations that produce a value return \c std::optional<T>.
///          - Operations that just act on the device return \c bool
///            (\c true on success).
///          - The most recent low-level Bosch error code is preserved in
///            \ref sentinel::bme280::last_error() so callers retain full
///            diagnostic
///            information without paying the call-site cost of
///            \c std::variant or wide return tuples.
///          - The "value-or-nullopt" idiom keeps the happy path compact;
///            the forensic accessor keeps `int8_t` granularity recoverable
///            from a debugger or log line.
///
///          Default configuration (applied during construction):
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
///          Thread safety:
///          - An instance is single-task-owned by convention; the cached
///            \c bme280_dev and \c last_error byte are not protected from
///            concurrent access. Wrap externally if you share a driver
///            across FreeRTOS tasks.
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.1 - optional/bool public API + last_error() accessor
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

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace sentinel {

///
/// \brief BME280 temperature/pressure/humidity sensor driver class
///
/// \details Provides a high-level C++ interface to the Bosch BME280 combined
///          environmental sensor. Each public member maps to a Bosch
///          \c bme280_* function but adapts the return shape to idiomatic
///          C++17: \c std::optional<T> for value-producing reads, \c bool
///          for setters and actions, and an instance-scoped
///          \ref last_error() accessor for the raw \c int8_t Bosch error
///          code from the most recent operation.
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
class bme280 {
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
    /// \details Wires the Bosch \c bme280_dev structure to this driver's own
    ///          \c bosch_read / \c bosch_write / \c bosch_delay adapter statics
    ///          (which frame register access over the transport's
    ///          vendor-agnostic byte I/O), performs \c bme280_init() to read
    ///          factory calibration, issues
    ///          a soft reset, and applies the default configuration described
    ///          in the file header.
    ///
    /// \param bus Reference to the transport instance (non-owning). Must
    ///            outlive this driver.
    /// \param device_address I²C target address. Defaults to
    ///                       \c BME280_I2C_ADDR_PRIM (0x76). Ignored when
    ///                       \c Transport is an SPI transport.
    ///
    /// \note Construction is silent on failure. To check whether the part
    ///       came up cleanly, call \ref last_error() immediately after
    ///       construction (it will be \c BME280_OK on a successful init) or
    ///       follow with a \ref read_chip_id call.
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

        m_device.read = &bme280::bosch_read;
        m_device.write = &bme280::bosch_write;
        m_device.delay_us = &bme280::bosch_delay;
        m_device.intf_ptr = &bus;

        // Each helper below sets m_last_error; the constructor short-circuits
        // on the first failure so the user can inspect last_error() to learn
        // which step did not complete.
        if (!_init()) {
            return;
        }
        if (!soft_reset()) {
            return;
        }

        _configure_sensor();
    }

    /// Non-copyable: holds a non-owning reference and a mutable Bosch device
    /// structure containing per-instance calibration data, neither of which
    /// can be meaningfully copied.
    bme280(const bme280 &) = delete;
    bme280 &operator=(const bme280 &) = delete;

    /// \brief Move-construct from another instance (defaulted).
    bme280(bme280 &&) noexcept = default;
    /// \brief Move-assign from another instance (defaulted).
    /// \return Reference to this instance.
    bme280 &operator=(bme280 &&) noexcept = default;

    // =====================================================================
    // Forensic accessor
    // =====================================================================

    ///
    /// \brief Return the raw Bosch error code from the most recent operation
    ///
    /// \details Updated by every member function that issues bus traffic.
    ///          On a successful operation this reads \c BME280_OK (0); on
    ///          failure it reads the exact \c int8_t code the Bosch C driver
    ///          returned (\c BME280_E_COMM_FAIL, \c BME280_E_NULL_PTR,
    ///          \c BME280_E_INVALID_LEN, ...). Pair this with \c logi /
    ///          \c loge to surface the underlying reason a
    ///          \c std::optional read came back \c std::nullopt.
    ///
    /// \return Most recent Bosch result code, or \c BME280_OK if no
    ///         operation has been attempted yet.
    ///
    int8_t last_error() const noexcept { return m_last_error; }

    // =====================================================================
    // Addressing (I²C-only convenience)
    // =====================================================================

    ///
    /// \brief Change I²C target address at runtime (I²C transports only)
    ///
    /// \param address New 7-bit I²C address — typically
    ///                \c BME280_I2C_ADDR_PRIM (0x76) or
    ///                \c BME280_I2C_ADDR_SEC (0x77).
    /// \return \c true on success. For SPI transports this is a no-op that
    ///         returns \c true.
    ///
    /// \details The BME280 supports two factory I²C addresses, selected by
    ///          the SDO pin (GND → primary, VDDIO → secondary). Use this to
    ///          retarget when multiple BME280s share a bus.
    ///
    /// \note This is a transport-level operation; it does not modify
    ///       \ref last_error().
    ///
    bool set_target_address(uint16_t address) noexcept {
        if constexpr (is_i2c) {
            return m_bus.set_target_address(address) == CY_RSLT_SUCCESS;
        } else {
            sentinel::unused(address);
            return true;
        }
    }

    // =====================================================================
    // Register access (low-level escape hatch)
    // =====================================================================

    ///
    /// \brief Write data to sensor registers
    ///
    /// \details Wrapper for \c bme280_set_regs(). Performs a multi-byte
    ///          scatter register write where the \c i-th byte of
    ///          \p reg_data is written to the register address at
    ///          \c reg_addr[i] (Bosch's intentionally unusual signature).
    ///
    /// \param[in] reg_addr Pointer to register addresses to write
    /// \param[in] reg_data Pointer to data buffer
    /// \param      len     Number of bytes to write
    /// \return \c true on success; on failure, \c last_error() carries the
    ///         Bosch error code.
    ///
    bool set_regs(uint8_t *reg_addr, const uint8_t *reg_data,
                  uint32_t len) const noexcept {
        m_last_error = bme280_set_regs(reg_addr, reg_data, len, &m_device);
        return m_last_error == BME280_OK;
    }

    ///
    /// \brief Read data from sensor registers
    ///
    /// \details Wrapper for \c bme280_get_regs(). Performs a multi-byte
    ///          register read into a caller-supplied buffer.
    ///
    /// \param      reg_addr  Starting register address
    /// \param[out] reg_data  Pointer to buffer for read data
    /// \param      len       Number of bytes to read
    /// \return \c true on success; on failure, \c last_error() carries the
    ///         Bosch error code.
    ///
    bool get_regs(uint8_t reg_addr, uint8_t *reg_data,
                  uint32_t len) const noexcept {
        m_last_error = bme280_get_regs(reg_addr, reg_data, len, &m_device);
        return m_last_error == BME280_OK;
    }

    // =====================================================================
    // Sensor settings
    // =====================================================================

    ///
    /// \brief Apply sensor configuration parameters
    ///
    /// \details Wrapper for \c bme280_set_sensor_settings(). \p
    ///          desired_settings is a bitmask of \c BME280_SEL_* flags
    ///          selecting which fields of \p settings should actually be
    ///          written to the device.
    ///
    /// \param desired_settings Bitmask of \c BME280_SEL_OSR_PRESS,
    ///                         \c BME280_SEL_OSR_TEMP, \c BME280_SEL_OSR_HUM,
    ///                         \c BME280_SEL_FILTER, \c BME280_SEL_STANDBY,
    ///                         or \c BME280_SEL_ALL_SETTINGS.
    /// \param settings         Configuration to apply.
    /// \return \c true on success; on failure, \c last_error() carries the
    ///         Bosch error code.
    ///
    bool set_sensor_settings(uint8_t desired_settings,
                             const bme280_settings &settings) const noexcept {
        m_last_error =
            bme280_set_sensor_settings(desired_settings, &settings, &m_device);
        return m_last_error == BME280_OK;
    }

    ///
    /// \brief Read current sensor configuration
    ///
    /// \details Wrapper for \c bme280_get_sensor_settings().
    ///
    /// \return Current sensor configuration on success;
    ///         \c std::nullopt on failure (consult \ref last_error()).
    ///
    std::optional<bme280_settings> sensor_settings() const noexcept {
        auto settings = bme280_settings{};
        m_last_error = bme280_get_sensor_settings(&settings, &m_device);
        if (m_last_error != BME280_OK) {
            return std::nullopt;
        }
        return settings;
    }

    // =====================================================================
    // Power mode
    // =====================================================================

    ///
    /// \brief Set sensor power mode
    ///
    /// \details Wrapper for \c bme280_set_sensor_mode().
    ///
    /// \param sensor_mode One of:
    ///                    - \c BME280_POWERMODE_SLEEP  (0)
    ///                    - \c BME280_POWERMODE_FORCED (1) — one-shot
    ///                    - \c BME280_POWERMODE_NORMAL (3) — continuous
    /// \return \c true on success; on failure, \c last_error() carries the
    ///         Bosch error code.
    ///
    bool set_sensor_mode(uint8_t sensor_mode) const noexcept {
        m_last_error = bme280_set_sensor_mode(sensor_mode, &m_device);
        return m_last_error == BME280_OK;
    }

    ///
    /// \brief Get current sensor power mode
    ///
    /// \details Wrapper for \c bme280_get_sensor_mode().
    ///
    /// \return Current power mode (\c BME280_POWERMODE_SLEEP,
    ///         \c BME280_POWERMODE_FORCED, or \c BME280_POWERMODE_NORMAL)
    ///         on success; \c std::nullopt on failure.
    ///
    std::optional<uint8_t> sensor_mode() const noexcept {
        auto mode = uint8_t{};
        m_last_error = bme280_get_sensor_mode(&mode, &m_device);
        if (m_last_error != BME280_OK) {
            return std::nullopt;
        }
        return mode;
    }

    // =====================================================================
    // System
    // =====================================================================

    ///
    /// \brief Perform sensor soft reset
    ///
    /// \details Wrapper for \c bme280_soft_reset(). Writes the soft-reset
    ///          command to register \c 0xE0, returning the sensor to its
    ///          power-on configuration. Calibration coefficients are
    ///          preserved in this driver's cached \c bme280_dev.
    ///
    /// \return \c true on success; on failure, \c last_error() carries the
    ///         Bosch error code.
    ///
    bool soft_reset() const noexcept {
        m_last_error = bme280_soft_reset(&m_device);
        return m_last_error == BME280_OK;
    }

    // =====================================================================
    // Sensor data
    // =====================================================================

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
    /// \return Compensated measurements on success; \c std::nullopt on
    ///         failure.
    ///
    std::optional<bme280_data> sensor_data(uint8_t sensor_comp) const noexcept {
        auto data = bme280_data{};
        m_last_error = bme280_get_sensor_data(sensor_comp, &data, &m_device);
        if (m_last_error != BME280_OK) {
            return std::nullopt;
        }
        return data;
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
    /// \return Compensated measurements on success; \c std::nullopt on
    ///         failure.
    ///
    std::optional<bme280_data>
    compensate_data(uint8_t sensor_comp,
                    const bme280_uncomp_data &uncomp_data) const noexcept {
        auto comp_data = bme280_data{};
        m_last_error = bme280_compensate_data(sensor_comp, &uncomp_data,
                                              &comp_data, &m_device.calib_data);
        if (m_last_error != BME280_OK) {
            return std::nullopt;
        }
        return comp_data;
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
    /// \param settings Oversampling configuration to query.
    /// \return Required delay in microseconds on success; \c std::nullopt
    ///         on failure.
    ///
    std::optional<uint32_t>
    cal_meas_delay(const bme280_settings &settings) const noexcept {
        auto delay = uint32_t{};
        m_last_error = bme280_cal_meas_delay(&delay, &settings);
        if (m_last_error != BME280_OK) {
            return std::nullopt;
        }
        return delay;
    }

    // =====================================================================
    // Convenience helpers
    // =====================================================================

    ///
    /// \brief Read the BME280 chip identifier
    ///
    /// \details Convenience wrapper that reads register \c 0xD0
    ///          (\c BME280_REG_CHIP_ID). A working BME280 always returns
    ///          \c 0x60 (\c BME280_CHIP_ID); use this as a presence and
    ///          bus-sanity check after construction.
    ///
    /// \return Chip ID byte on success; \c std::nullopt on failure.
    ///
    std::optional<uint8_t> read_chip_id() const noexcept {
        auto id = uint8_t{};
        if (!get_regs(BME280_REG_CHIP_ID, &id, sizeof(id))) {
            return std::nullopt;
        }
        return id;
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
    ///          On success, the returned value holds the compensated
    ///          readings in the units selected by the Bosch driver's
    ///          compile-time option:
    ///          - \c BME280_DOUBLE_ENABLE (default): Pa, °C, %RH as
    ///            \c double values.
    ///          - 32-bit fixed-point fallback: pressure in Pa as
    ///            \c uint32_t, temperature in 1/100 °C as \c int32_t,
    ///            humidity in Q22.10 as \c uint32_t.
    ///
    /// \return Compensated T/P/H sample on success; \c std::nullopt on
    ///         failure.
    ///
    /// \note Each call triggers a fresh measurement, so the minimum sensible
    ///       interval between calls is the delay returned by
    ///       \c bme280_cal_meas_delay() (~10 ms at the default 16x
    ///       oversampling configuration).
    ///
    std::optional<bme280_data> read_sensor_data() const noexcept {
        if (!set_sensor_mode(BME280_POWERMODE_FORCED)) {
            return std::nullopt;
        }

        auto settings = sensor_settings();
        if (!settings) {
            return std::nullopt;
        }

        auto delay_microseconds = cal_meas_delay(*settings);
        if (!delay_microseconds) {
            return std::nullopt;
        }

        m_bus.delay_us(*delay_microseconds);

        return sensor_data(BME280_ALL);
    }

private:
    // =====================================================================
    // Internal helpers
    // =====================================================================

    ///
    /// \brief Initialize the underlying Bosch device handle
    ///
    /// \details Thin wrapper for \c bme280_init(). Performs the initial
    ///          chip-ID handshake and reads factory calibration into
    ///          \c m_device.calib_data.
    ///
    /// \return \c true on success; on failure, \c last_error() carries the
    ///         Bosch error code.
    ///
    bool _init() noexcept {
        m_last_error = bme280_init(&m_device);
        return m_last_error == BME280_OK;
    }

    ///
    /// \brief Apply this driver's default sensor configuration
    ///
    /// \details Programs the oversampling, IIR filter, and standby settings
    ///          described in the file header. Called once at the end of
    ///          construction.
    ///
    /// \return \c true on success; on failure, \c last_error() carries the
    ///         Bosch error code.
    ///
    bool _configure_sensor() noexcept {
        auto settings = bme280_settings{};

        // Maximum oversampling on all three channels for highest precision.
        settings.osr_p = BME280_OVERSAMPLING_16X;
        settings.osr_t = BME280_OVERSAMPLING_16X;
        settings.osr_h = BME280_OVERSAMPLING_16X;

        // Aggressive IIR filtering smooths out short-term noise without
        // killing responsiveness.
        settings.filter = BME280_FILTER_COEFF_16;

        // Standby is only consulted in normal mode; pick the shortest value.
        settings.standby_time = BME280_STANDBY_TIME_0_5_MS;

        auto settings_sel = static_cast<uint8_t>(
            BME280_SEL_OSR_PRESS | BME280_SEL_OSR_TEMP | BME280_SEL_OSR_HUM |
            BME280_SEL_FILTER | BME280_SEL_STANDBY);

        return set_sensor_settings(settings_sel, settings);
    }

    // =====================================================================
    // Bosch Sensortec C-driver adapter (function-pointer ABI)
    // =====================================================================
    //
    // These statics implement the Bosch \c bme280_dev read/write/delay
    // callback ABI. They live in the driver (not the transport) so the
    // transports stay vendor-agnostic byte-movers: the adapter knows the
    // BME280's register-framing convention, then forwards the actual bytes
    // to the transport's generic read/write/write_read/delay_us. \c intf_ptr
    // is the transport instance (\c &m_bus, set in the constructor); each
    // static casts it back to \c Transport* and branches the framing on the
    // compile-time \ref is_i2c / \ref is_spi tag.

    ///
    /// \brief Bosch read callback: read \p length bytes starting at
    ///        \p reg_addr into \p reg_data.
    ///
    /// \details I²C frames a register-pointer write followed by a
    ///          repeated-start read; SPI sets the address MSB (read
    ///          direction, \c reg_addr | 0x80) then clocks the response.
    ///
    /// \param reg_addr Starting register address to read from.
    /// \param reg_data Destination buffer.
    /// \param length   Number of bytes to read.
    /// \param intf_ptr Transport instance (a \c Transport*).
    /// \return \c 0 on success, \c -1 on transport failure (Bosch ABI).
    ///
    static int8_t bosch_read(uint8_t reg_addr, uint8_t *reg_data,
                             uint32_t length, void *intf_ptr) noexcept {
        auto *self = static_cast<Transport *>(intf_ptr);

        if constexpr (is_i2c) {
            auto rc = self->write_read(&reg_addr, sizeof(reg_addr), reg_data,
                                       length, /*timeout_on_write=*/100,
                                       /*timeout_on_read=*/100,
                                       /*send_stop_on_write=*/false,
                                       /*send_stop_on_read=*/true);
            return rc == CY_RSLT_SUCCESS ? int8_t{0} : int8_t{-1};
        } else {
            auto cmd = static_cast<uint8_t>(reg_addr | 0x80);
            return static_cast<int8_t>(
                self->write_read(&cmd, sizeof(cmd), reg_data, length));
        }
    }

    ///
    /// \brief Bosch write callback: write \p length bytes of \p reg_data
    ///        starting at \p reg_addr.
    ///
    /// \details Both protocols emit \c [reg_addr, data...] in one contiguous
    ///          transfer; SPI clears the address MSB (write direction,
    ///          \c reg_addr & 0x7F). The 256-byte scratch is comfortably
    ///          above any BME280 write payload; oversized transfers fail
    ///          rather than overflow.
    ///
    /// \param reg_addr Starting register address to write.
    /// \param reg_data Source buffer.
    /// \param length   Number of bytes to write.
    /// \param intf_ptr Transport instance (a \c Transport*).
    /// \return \c 0 on success, \c -1 on oversized payload or transport
    ///         failure (Bosch ABI).
    ///
    static int8_t bosch_write(uint8_t reg_addr, const uint8_t *reg_data,
                              uint32_t length, void *intf_ptr) noexcept {
        auto *self = static_cast<Transport *>(intf_ptr);

        auto buffer = std::array<uint8_t, 256>{};
        if (length + 1 > buffer.size()) {
            return -1;
        }
        std::copy(reg_data, reg_data + length, buffer.data() + 1);

        if constexpr (is_i2c) {
            buffer[0] = reg_addr;
            auto rc = self->write(buffer.data(), length + 1,
                                  /*timeout_ms=*/100, /*send_stop=*/true);
            return rc == CY_RSLT_SUCCESS ? int8_t{0} : int8_t{-1};
        } else {
            buffer[0] = static_cast<uint8_t>(reg_addr & 0x7F);
            return static_cast<int8_t>(self->write(buffer.data(), length + 1));
        }
    }

    ///
    /// \brief Bosch delay callback: busy-wait \p period microseconds.
    ///
    /// \details Forwards to the transport's microsecond delay; no bus access
    ///          is involved.
    ///
    /// \param period   Delay duration in microseconds.
    /// \param intf_ptr Transport instance (a \c Transport*).
    ///
    static void bosch_delay(uint32_t period, void *intf_ptr) noexcept {
        auto *self = static_cast<Transport *>(intf_ptr);
        self->delay_us(period);
    }

    Transport &m_bus;              ///< Non-owning reference to the transport.
    mutable bme280_dev m_device{}; ///< Bosch BME280 device handle (mutable
                                   ///< because all Bosch C functions take a
                                   ///< non-const \c bme280_dev*, even on
                                   ///< logically read-only operations).
    mutable int8_t m_last_error{BME280_OK}; ///< Raw error code from the
                                            ///< most recent Bosch call;
                                            ///< exposed by \ref last_error().
};

///
/// \ingroup transport
/// \brief Convenience alias for an I2C-backed \ref sentinel::bme280 instance.
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
/// \brief Convenience alias for a SPI-backed \ref sentinel::bme280 instance.
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

#endif /* SENTINEL_BME280_HPP */
