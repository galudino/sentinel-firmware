///
/// \file    sentinel_spi_flash_bus_controller.hpp
/// \brief   SPI flash bus controller/helper (platform-agnostic CS/DC/RESET
/// control)
///
/// \details Adapts an SPI byte transport (CRTP)
///
/// \date    2024–2025
/// \version 1.0
///

#ifndef SENTINEL_SPI_FLASH_BUS_HPP
#define SENTINEL_SPI_FLASH_BUS_HPP

#include "sentinel_byte_transport.hpp"
#include "sentinel_gpio_line.hpp"
#include "sentinel_span.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace sentinel {

///
/// \brief Logical description of flash-related control lines
///
/// \details
/// - Reset (optional)
/// - Chip Select
///
struct spi_flash_pins {
    sentinel::gpio_line reset{}; ///< Reset (optional)
    sentinel::gpio_line cs{};    ///< Chip Select
};

template <typename SPITransport>
class spi_flash_bus_controller;

} // namespace sentinel

///
/// \tparam SPITransport Transport deriving from byte_transport<..., spi_tag>
///
template <typename SPITransport>
class sentinel::spi_flash_bus_controller {
    static_assert(std::is_base_of<byte_transport<SPITransport, spi_tag>,
                                  SPITransport>::value,
                  "SPITransport must derive from "
                  "sentinel::byte_transport<SPITransport, spi_tag>");

public:
    ///
    /// \brief Construct over a transport, pins, and delay API
    ///
    explicit spi_flash_bus_controller(SPITransport &spi,
                                      const spi_flash_pins &pins) noexcept
        : m_spi(spi), m_pins(pins) {}

    ///
    /// \brief Set control lines to idle/inactive state
    ///
    /// \details Initializes all control lines to their idle states:
    ///          - CS: inactive (deasserted)
    ///          - RESET: deasserted (inactive)
    ///
    /// \note This should be called once during display initialization to
    ///       ensure all control lines start in known states.
    ///
    auto prime() const noexcept {
        // CS idle = inactive (true means "deassert" at the logical level)
        set_chip_enable(false);
        // RESET idle = deassert (true)
        set_reset(false);
    }

    ///
    /// \brief Perform hardware reset sequence
    ///
    /// \details Generates a hardware reset pulse by:
    ///          1. Asserting RESET (active)
    ///          2. Waiting assert_ms milliseconds
    ///          3. Deasserting RESET (inactive)
    ///          4. Waiting settle_ms milliseconds for device to stabilize
    ///
    /// \param assert_ms Duration to hold RESET asserted (default: 10ms)
    /// \param settle_ms Duration to wait after reset (default: 120ms)
    ///
    /// \note No-op if RESET line is not valid or delay_api is not valid.
    ///       The reset timing should match the display controller's datasheet.
    ///
    auto reset_pulse(uint32_t assert_ms = 10,
                     uint32_t settle_ms = 120) const noexcept {
        if (!m_pins.reset.valid()) {
            return;
        }

        set_reset(true);
        m_spi.delay(assert_ms);
        set_reset(false);
        m_spi.delay(settle_ms);
    }

    ///
    /// \brief Combined transfer (blocking) with automatic CS management
    ///
    /// \param tx  Pointer to transmit buffer
    /// \param tx_size Number of bytes to transmit
    /// \param rx  Pointer to receive buffer
    /// \param rx_size Number of bytes to receive
    /// \return Implementation-specific status/result code
    ///
    /// \details Asserts CS before the transfer and deasserts CS after
    ///          completion, ensuring a single continuous SPI transaction.
    ///          This is required by the AT45DB641E (and most SPI flash)
    ///          which treats CS deassertion as the end of a command frame.
    ///
    auto transfer(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                  size_t rx_size) noexcept {
        chip_enable();
        auto result = m_spi.transfer(tx, tx_size, rx, rx_size);
        chip_disable();
        return result;
    }

    ///
    /// \brief Combined transfer (non-blocking) with automatic CS management
    ///
    /// \param tx  Pointer to transmit buffer
    /// \param tx_size Number of bytes to transmit
    /// \param rx  Pointer to receive buffer
    /// \param rx_size Number of bytes to receive
    /// \return Implementation-specific status/result code
    ///
    /// \note Caller must ensure CS remains asserted until async transfer
    ///       completes. Consider using the blocking transfer() instead
    ///       unless async behavior is specifically required.
    ///
    auto transfer_async(const uint8_t *tx, size_t tx_size, uint8_t *rx,
                        size_t rx_size) noexcept {
        chip_enable();
        return m_spi.transfer_async(tx, tx_size, rx, rx_size);
    }

    ///
    /// \brief Get reference to underlying SPI transport
    ///
    /// \return Reference to SPI transport
    ///
    SPITransport &transport() noexcept { return m_spi; }

    ///
    /// \brief Get const reference to underlying SPI transport
    ///
    /// \return Const reference to SPI transport
    ///
    const SPITransport &transport() const noexcept { return m_spi; }

    ///
    /// \brief Assert chip select (activate device)
    ///
    /// \details Sets chip select to active state. Does nothing if CS pin is NC.
    ///
    auto chip_enable() const noexcept {
        set_chip_enable(true);
        // If the transport also exposes chip_enable(), it’s harmless to skip it
        // here: we keep CS control in one place (this bus).
    }

    ///
    /// \brief Deassert chip select (deactivate device)
    ///
    /// \details Sets chip select to inactive state. Does nothing if CS pin is
    /// NC.
    ///
    auto chip_disable() const noexcept { set_chip_enable(false); }

    ///
    /// \brief Control RESET line state
    ///
    /// \details Asserts or deasserts the hardware RESET line.
    ///
    /// \param asserted true to assert RESET (active), false to deassert
    ///
    /// \note No-op if RESET line is not valid. Polarity is handled by the
    ///       gpio_line adapter (active-low/active-high).
    ///
    auto set_reset(bool asserted) const noexcept {
        if (!m_pins.reset.valid()) {
            return;
        }

        // asserted = true means logical "assert"
        // Adapter handles polarity: if active_low=true, it inverts
        m_pins.reset.set(asserted);
    }

private:
    /// \brief Control CS line state
    ///
    /// \details Asserts or deasserts the chip select line.
    ///
    /// \param asserted true to select device (CS active), false to deselect
    ///
    /// \note No-op if CS line is not valid. Polarity is handled by the
    ///       gpio_line adapter (active-low/active-high).
    ///
    auto set_chip_enable(bool asserted) const noexcept {
        if (!m_pins.cs.valid()) {
            return;
        }

        // asserted = true means "select" (CS active)
        // Adapter handles polarity: if active_low=true, it inverts to LOW
        m_pins.cs.set(asserted);
    }

private:
    SPITransport &m_spi;
    spi_flash_pins m_pins;
};

#endif /* SENTINEL_SPI_FLASH_BUS_HPP */
