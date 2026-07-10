///
/// \file    sentinel_w25q128.hpp
/// \brief   Winbond W25Q128JV 128 Mbit (16 MB) SPI NOR flash driver
///
/// \details This header provides a complete, datasheet-derived driver for
///          the Winbond W25Q128JV serial NOR flash. The driver is pure C++;
///          no underlying vendor C library is wrapped.
///
///          Every opcode documented in the W25Q128JV datasheet (Rev. K /
///          Aug 2024 or equivalent) has a named entry in \ref opcode,
///          even ones whose member-function counterpart is not implemented
///          in this skeleton (Dual / Quad I/O variants require multi-bit
///          SPI configuration of the SCB that we are not enabling for
///          phase 1). Bit positions for the three status registers are
///          collected in \ref status_register_1, \ref status_register_2,
///          and \ref status_register_3 nested types so callers never refer
///          to magic numbers.
///
///          Public API design (matches \ref sentinel::bme280 /
///          \ref sentinel::ds3231):
///          - Value-producing reads return \c std::optional<T>.
///          - Setters / actions return \c bool (\c true on success).
///          - The most recent error is exposed via \ref last_error(),
///            typed as \ref err.
///
///          Memory layout:
///          - Total: 16 MiB (24-bit byte address space, 0x000000–0xFFFFFF)
///          - Page: 256 B (programming unit)
///          - Sector: 4 KiB (smallest erase unit)
///          - 32 KiB and 64 KiB blocks: larger erase units
///          - 3 × 256-byte security registers, independently erasable /
///            programmable, addressed 0x0010xx / 0x0020xx / 0x0030xx
///
///          Bus usage:
///          - This driver is parameterised over a \c Transport conforming
///            to \c sentinel::byte_transport<_, spi_tag>. The typical
///            transport is \c sentinel::cyhal_spi_bus_transport, which
///            funnels every transaction through
///            \c sentinel::task::spi_bus so concurrent SPI peripherals
///            share the bus cleanly.
///          - Address mode: 3-byte (W25Q128JV's 16 MiB fits in 24 bits;
///            4-byte address mode is enumerated for completeness but not
///            used).
///
/// \author  galudino
/// \date    2026-05-18
/// \version 1.0 - Datasheet-derived W25Q128 driver
///

#ifndef SENTINEL_W25Q128_HPP
#define SENTINEL_W25Q128_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "FreeRTOS.h"
#include "cy_result.h"
#include "semphr.h"
}
#pragma GCC diagnostic pop

#include "sentinel_byte_transport.hpp"
#include "sentinel_span.hpp"
#include "sentinel_utilities.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace sentinel {

template <typename Transport>
class w25q128;

} // namespace sentinel

///
/// \brief Winbond W25Q128JV driver class.
///
/// \details Provides a high-level C++ interface to the W25Q128 SPI NOR
///          flash. The class exposes every datasheet feature relevant to
///          single-bit-SPI operation as a typed member function: JEDEC /
///          manufacturer / unique-ID reads, all three status registers,
///          read / fast-read, page program, three erase granularities
///          (4 KiB / 32 KiB / 64 KiB / full chip), three security
///          registers, SFDP, software reset, power-down / release, and
///          write enable/disable.
///
/// \tparam Transport Transport implementation deriving from
///                   \c sentinel::byte_transport<Transport, spi_tag>.
///                   The CS pin for this device must be configured as
///                   one of the SCB's SS0..SS3 lines in Device
///                   Configurator.
///
template <typename Transport>
class sentinel::w25q128 {
    static_assert(std::is_base_of_v<byte_transport<Transport, spi_tag>,
                                    Transport>,
                  "Transport must derive from "
                  "sentinel::byte_transport<Transport, spi_tag>");

public:
    // =====================================================================
    // Error type
    // =====================================================================

    enum class err : int8_t {
        ok                = 0,
        transport_failure = -1, ///< Underlying SPI transport call failed.
        invalid_argument  = -2, ///< Out-of-range address / size / index.
        write_not_enabled = -3, ///< WEL was not set after \c write_enable.
        busy_timeout      = -4, ///< BUSY did not clear within the polling
                                ///< budget for an erase / program op.
    };

    // =====================================================================
    // Layout constants
    // =====================================================================

    /// Total flash size: 128 Mbit = 16 MiB.
    static constexpr uint32_t TOTAL_SIZE_BYTES = 16u * 1024u * 1024u;

    /// Page size — the largest single-shot program payload.
    static constexpr uint32_t PAGE_SIZE_BYTES = 256u;

    /// Smallest erase granularity.
    static constexpr uint32_t SECTOR_SIZE_BYTES = 4u * 1024u;

    static constexpr uint32_t BLOCK_32KB_SIZE_BYTES = 32u * 1024u;
    static constexpr uint32_t BLOCK_64KB_SIZE_BYTES = 64u * 1024u;

    /// Each security register is 256 bytes; there are three of them.
    static constexpr uint32_t SECURITY_REGISTER_SIZE_BYTES = 256u;
    static constexpr uint8_t  SECURITY_REGISTER_COUNT      = 3u;

    /// 64-bit factory-programmed unique ID.
    static constexpr size_t UNIQUE_ID_SIZE_BYTES = 8u;

    /// SFDP space is 256 bytes (read with a 3-byte address).
    static constexpr uint32_t SFDP_SIZE_BYTES = 256u;

    // =====================================================================
    // Identification constants (datasheet-fixed)
    // =====================================================================

    /// Expected first byte of JEDEC ID read (cmd 0x9F): Winbond.
    static constexpr uint8_t JEDEC_MANUFACTURER_ID = 0xEF;

    /// Expected second byte of JEDEC ID read: memory type for W25Q at SPI.
    static constexpr uint8_t JEDEC_MEMORY_TYPE = 0x40;

    /// Expected third byte of JEDEC ID read: 128 Mbit capacity code.
    static constexpr uint8_t JEDEC_CAPACITY = 0x18;

    /// Manufacturer ID from cmd 0x90 (also 0xEF for Winbond).
    static constexpr uint8_t MANUFACTURER_ID = 0xEF;

    /// Device ID from cmd 0x90: 0x17 for W25Q128JV.
    static constexpr uint8_t DEVICE_ID = 0x17;

    /// Device ID returned by cmd 0xAB (Release Power-Down / Device ID).
    static constexpr uint8_t RELEASE_POWER_DOWN_DEVICE_ID = 0x17;

    // =====================================================================
    // Opcode map (W25Q128JV datasheet)
    // =====================================================================

    enum class opcode : uint8_t {
        // ── Identification ────────────────────────────────────────────
        manufacturer_device_id          = 0x90,
        manufacturer_device_id_dual_io  = 0x92,
        manufacturer_device_id_quad_io  = 0x94,
        jedec_id                        = 0x9F,
        read_unique_id                  = 0x4B,

        // ── Write control ─────────────────────────────────────────────
        write_enable                    = 0x06,
        write_enable_volatile_sr        = 0x50,
        write_disable                   = 0x04,

        // ── Status registers ──────────────────────────────────────────
        read_status_register_1          = 0x05,
        read_status_register_2          = 0x35,
        read_status_register_3          = 0x15,
        write_status_register_1         = 0x01,
        write_status_register_2         = 0x31,
        write_status_register_3         = 0x11,

        // ── Read ─────────────────────────────────────────────────────
        read_data                       = 0x03,
        fast_read                       = 0x0B,
        fast_read_dual_output           = 0x3B,
        fast_read_quad_output           = 0x6B,
        fast_read_dual_io               = 0xBB,
        fast_read_quad_io               = 0xEB,
        word_read_quad_io               = 0xE7,
        octal_word_read_quad_io         = 0xE3,

        // ── Program ──────────────────────────────────────────────────
        page_program                    = 0x02,
        quad_page_program               = 0x32,

        // ── Erase ────────────────────────────────────────────────────
        sector_erase_4kb                = 0x20,
        block_erase_32kb                = 0x52,
        block_erase_64kb                = 0xD8,
        chip_erase                      = 0xC7,
        chip_erase_alt                  = 0x60,
        erase_program_suspend           = 0x75,
        erase_program_resume            = 0x7A,

        // ── Power / Reset ────────────────────────────────────────────
        power_down                      = 0xB9,
        release_power_down              = 0xAB,
        enable_reset                    = 0x66,
        reset_device                    = 0x99,

        // ── Security registers ───────────────────────────────────────
        erase_security_register         = 0x44,
        program_security_register       = 0x42,
        read_security_register          = 0x48,

        // ── SFDP ─────────────────────────────────────────────────────
        read_sfdp_register              = 0x5A,

        // ── 4-byte address mode (not needed for 16MB device) ─────────
        enter_4byte_address_mode        = 0xB7,
        exit_4byte_address_mode         = 0xE9,
        write_extended_address_register = 0xC5,
        read_extended_address_register  = 0xC8,
    };

    // =====================================================================
    // Status Register bit positions
    // =====================================================================

    struct status_register_1 {
        static constexpr uint8_t BUSY_BIT = 0;
        static constexpr uint8_t WEL_BIT  = 1;
        static constexpr uint8_t BP0_BIT  = 2;
        static constexpr uint8_t BP1_BIT  = 3;
        static constexpr uint8_t BP2_BIT  = 4;
        static constexpr uint8_t TB_BIT   = 5;
        static constexpr uint8_t SEC_BIT  = 6;
        static constexpr uint8_t SRP0_BIT = 7;
    };

    struct status_register_2 {
        static constexpr uint8_t SRP1_BIT = 0;
        static constexpr uint8_t QE_BIT   = 1;
        static constexpr uint8_t LB1_BIT  = 3;
        static constexpr uint8_t LB2_BIT  = 4;
        static constexpr uint8_t LB3_BIT  = 5;
        static constexpr uint8_t CMP_BIT  = 6;
        static constexpr uint8_t SUS_BIT  = 7;
    };

    struct status_register_3 {
        static constexpr uint8_t WPS_BIT      = 2;
        static constexpr uint8_t DRV0_BIT     = 5;
        static constexpr uint8_t DRV1_BIT     = 6;
        static constexpr uint8_t HOLD_RST_BIT = 7;
    };

    // =====================================================================
    // Domain types
    // =====================================================================

    ///
    /// \brief JEDEC ID payload returned by opcode 0x9F.
    ///
    struct jedec_id_data {
        uint8_t manufacturer; ///< 0xEF Winbond, 0xC8 GigaDevice, etc.
        uint8_t memory_type;  ///< Expected: 0x40 (W25Q SPI family).
        uint8_t capacity;     ///< Expected: 0x18 (128 Mbit).

        friend bool operator==(const jedec_id_data &a,
                               const jedec_id_data &b) noexcept {
            return a.manufacturer == b.manufacturer
                && a.memory_type  == b.memory_type
                && a.capacity     == b.capacity;
        }
        friend bool operator!=(const jedec_id_data &a,
                               const jedec_id_data &b) noexcept {
            return !(a == b);
        }
    };

    ///
    /// \brief Known-good JEDEC ID triples for W25Q128-compatible parts.
    ///
    /// \details The W25Q128JV is heavily cloned. Parts from GigaDevice,
    ///          XTX, Boya, ZBIT and others are functionally drop-in for
    ///          the Winbond original — same command set, same address
    ///          layout, same status-register semantics — but advertise
    ///          their own manufacturer byte in the 0x9F JEDEC response.
    ///          Memory-type (\c 0x40) and capacity (\c 0x18) are stable
    ///          across the ecosystem; only the manufacturer byte varies.
    ///
    ///          Use \ref is_known_jedec to test whether an observed
    ///          \ref jedec_id_data matches any entry in this list.
    ///          Add new entries here as new clones are physically
    ///          validated against the testbench.
    ///
    static constexpr std::array<jedec_id_data, 5> KNOWN_GOOD_JEDEC = {{
        { 0xEF, 0x40, 0x18 }, ///< Winbond W25Q128JV (the original).
        { 0xC8, 0x40, 0x18 }, ///< GigaDevice GD25Q128.
        { 0x0B, 0x40, 0x18 }, ///< XTX XT25F128B.
        { 0x68, 0x40, 0x18 }, ///< Boya BY25Q128AS.
        { 0x5E, 0x40, 0x18 }, ///< ZBIT ZB25VQ128.
    }};

    ///
    /// \brief Test whether a JEDEC ID triple matches any known-good entry.
    ///
    /// \param id JEDEC ID as returned by \ref jedec_id().
    /// \return \c true if \p id matches any entry in
    ///         \ref KNOWN_GOOD_JEDEC, \c false otherwise.
    ///
    static constexpr bool is_known_jedec(const jedec_id_data &id) noexcept {
        for (auto const &entry : KNOWN_GOOD_JEDEC) {
            if (id == entry) {
                return true;
            }
        }
        return false;
    }

    ///
    /// \brief Manufacturer + Device ID payload returned by opcode 0x90.
    ///
    struct mfr_dev_id_data {
        uint8_t manufacturer; ///< Expected: 0xEF (Winbond).
        uint8_t device;       ///< Expected: 0x17 (W25Q128JV).
    };

    // =====================================================================
    // Construction
    // =====================================================================

    ///
    /// \brief Construct the driver and bind it to a transport.
    ///
    /// \details No bus traffic during construction. The W25Q128 is
    ///          fully ready immediately after power-up (typical tVSL =
    ///          20 µs); the first read after construction will return
    ///          valid data.
    ///
    /// \param bus Reference to the SPI transport. Must outlive this
    ///            driver instance.
    /// \param device_mutex Optional recursive mutex serialising logical
    ///            operations on the physical chip. Pass
    ///            \c sentinel::resource::flash_device_mutex when more than one
    ///            task may touch this flash (the SPI bus arbiter only
    ///            serialises individual transactions, which is not enough to
    ///            keep a write-enable→program→poll sequence atomic — see that
    ///            mutex's documentation). When \c nullptr (the default) the
    ///            driver performs no locking, which is correct for strictly
    ///            single-task use.
    ///
    explicit w25q128(Transport &bus,
                     SemaphoreHandle_t device_mutex = nullptr) noexcept
        : m_bus(bus), m_device_mutex(device_mutex) {}

    w25q128(const w25q128 &)            = delete;
    w25q128 &operator=(const w25q128 &) = delete;
    w25q128(w25q128 &&) noexcept        = default;
    w25q128 &operator=(w25q128 &&) noexcept = default;

    err last_error() const noexcept { return m_last_error; }

    // =====================================================================
    // Identification
    // =====================================================================

    ///
    /// \brief Read the JEDEC ID (opcode 0x9F).
    ///
    std::optional<jedec_id_data> jedec_id() const noexcept {
        auto tx = std::array<uint8_t, 1>{
            static_cast<uint8_t>(opcode::jedec_id)};
        auto rx = std::array<uint8_t, 4>{}; // 1 byte cmd echo + 3 ID bytes

        if (!transact(tx.data(), tx.size(), rx.data(), rx.size())) {
            return std::nullopt;
        }

        auto out         = jedec_id_data{};
        out.manufacturer = rx[1];
        out.memory_type  = rx[2];
        out.capacity     = rx[3];
        return out;
    }

    ///
    /// \brief Read the manufacturer + device ID (opcode 0x90, addr 0).
    ///
    std::optional<mfr_dev_id_data> manufacturer_device_id() const noexcept {
        auto tx = std::array<uint8_t, 4>{
            static_cast<uint8_t>(opcode::manufacturer_device_id),
            0x00, 0x00, 0x00};
        auto rx = std::array<uint8_t, 6>{}; // 4 bytes cmd/addr echo + 2 ID

        if (!transact(tx.data(), tx.size(), rx.data(), rx.size())) {
            return std::nullopt;
        }

        auto out         = mfr_dev_id_data{};
        out.manufacturer = rx[4];
        out.device       = rx[5];
        return out;
    }

    ///
    /// \brief Read the 64-bit factory unique ID (opcode 0x4B).
    ///
    /// \details The unique ID is a serial-number-shaped 64-bit value
    ///          burned in at the factory. Useful as a per-device
    ///          identifier without needing to allocate one yourself.
    ///
    std::optional<std::array<uint8_t, UNIQUE_ID_SIZE_BYTES>>
    unique_id() const noexcept {
        // Opcode 0x4B + 4 dummy bytes + 8 ID bytes
        auto tx = std::array<uint8_t, 5>{
            static_cast<uint8_t>(opcode::read_unique_id),
            0, 0, 0, 0};
        auto rx = std::array<uint8_t, 5 + UNIQUE_ID_SIZE_BYTES>{};

        if (!transact(tx.data(), tx.size(), rx.data(), rx.size())) {
            return std::nullopt;
        }

        auto out = std::array<uint8_t, UNIQUE_ID_SIZE_BYTES>{};
        std::copy(rx.begin() + 5, rx.end(), out.begin());
        return out;
    }

    ///
    /// \brief Release power-down and read the legacy 1-byte device ID
    ///        (opcode 0xAB).
    ///
    /// \details Also performs the release-power-down side effect — after
    ///          this call the device is responsive even if it had been
    ///          put into deep power-down via \ref power_down. Returns
    ///          the 1-byte device ID (\c 0x17 for W25Q128JV).
    ///
    std::optional<uint8_t> release_power_down_device_id() noexcept {
        auto tx = std::array<uint8_t, 4>{
            static_cast<uint8_t>(opcode::release_power_down),
            0, 0, 0};
        auto rx = std::array<uint8_t, 5>{};

        if (!transact(tx.data(), tx.size(), rx.data(), rx.size())) {
            return std::nullopt;
        }
        return rx[4];
    }

    // =====================================================================
    // Status registers
    // =====================================================================

    std::optional<uint8_t> read_status_register_1() const noexcept {
        return read_one_byte_register(opcode::read_status_register_1);
    }
    std::optional<uint8_t> read_status_register_2() const noexcept {
        return read_one_byte_register(opcode::read_status_register_2);
    }
    std::optional<uint8_t> read_status_register_3() const noexcept {
        return read_one_byte_register(opcode::read_status_register_3);
    }

    ///
    /// \brief Write Status Register 1.
    ///
    /// \param value         New SR1 value.
    /// \param volatile_only When \c true, use the volatile-write opcode
    ///                      (\c 0x50) so the change persists only until
    ///                      power-cycle; when \c false (default), commit
    ///                      to the non-volatile OTP.
    ///
    bool write_status_register_1(uint8_t value,
                                 bool volatile_only = false) noexcept {
        return write_status_register(opcode::write_status_register_1, value,
                                     volatile_only);
    }
    bool write_status_register_2(uint8_t value,
                                 bool volatile_only = false) noexcept {
        return write_status_register(opcode::write_status_register_2, value,
                                     volatile_only);
    }
    bool write_status_register_3(uint8_t value,
                                 bool volatile_only = false) noexcept {
        return write_status_register(opcode::write_status_register_3, value,
                                     volatile_only);
    }

    ///
    /// \brief Read the BUSY flag (SR1 bit 0).
    ///
    std::optional<bool> is_busy() const noexcept {
        auto sr1 = read_status_register_1();
        if (!sr1) return std::nullopt;
        return (*sr1 & (1u << status_register_1::BUSY_BIT)) != 0;
    }

    ///
    /// \brief Read the WEL (Write Enable Latch) flag (SR1 bit 1).
    ///
    std::optional<bool> is_write_enabled() const noexcept {
        auto sr1 = read_status_register_1();
        if (!sr1) return std::nullopt;
        return (*sr1 & (1u << status_register_1::WEL_BIT)) != 0;
    }

    ///
    /// \brief Block until BUSY clears, or until \p timeout_ms elapses.
    ///
    /// \details Polls SR1 at \p poll_interval_ms cadence. Long-running
    ///          operations (chip erase ~25 s typical, 200 s max) need
    ///          generous timeouts; per-sector erase typically completes
    ///          in 30–400 ms.
    ///
    bool wait_until_ready(uint32_t timeout_ms = 30000,
                          uint32_t poll_interval_ms = 1) noexcept {
        auto guard = lock();
        auto elapsed = uint32_t{0};
        while (elapsed < timeout_ms) {
            auto busy = is_busy();
            if (!busy) return false; // transport error already in m_last_error
            if (!*busy) return true;

            m_bus.delay(poll_interval_ms);
            elapsed += poll_interval_ms;
        }
        m_last_error = err::busy_timeout;
        return false;
    }

    ///
    /// \brief Block until a write/erase/program completes: BUSY clear \b and WEL
    ///        clear.
    ///
    /// \details A completed write/erase/program auto-clears the Write Enable
    ///          Latch (WEL, SR1 bit 1) — JEDEC-standard for all W25Q/GD25Q
    ///          parts. Waiting on BUSY alone is racy: the very first poll can
    ///          read \c BUSY==0 \e before the chip has asserted BUSY for the
    ///          freshly issued op, so a still-running (or never-started) erase
    ///          looks "done" and yields a false success (#56 — intermittently
    ///          seen as "post-erase region not blank" with no error). Requiring
    ///          WEL to have auto-cleared closes both cases: while the op runs (or
    ///          if BUSY is polled early) WEL is still set, so we keep waiting; if
    ///          the op never executed, WEL stays set and this times out honestly
    ///          instead of returning a false success. Same single SR1 read per
    ///          poll — no extra bus traffic, no added latency in the normal case
    ///          (WEL and BUSY both clear at completion).
    ///
    bool wait_until_write_complete(uint32_t timeout_ms = 30000,
                                   uint32_t poll_interval_ms = 1) noexcept {
        auto guard = lock();
        auto elapsed = uint32_t{0};
        while (elapsed < timeout_ms) {
            auto sr1 = read_status_register_1();
            if (!sr1) {
                return false; // transport error already in m_last_error
            }
            const bool busy = (*sr1 & (1u << status_register_1::BUSY_BIT)) != 0;
            const bool wel = (*sr1 & (1u << status_register_1::WEL_BIT)) != 0;
            if (!busy && !wel) {
                return true;
            }
            m_bus.delay(poll_interval_ms);
            elapsed += poll_interval_ms;
        }
        m_last_error = err::busy_timeout;
        return false;
    }

    // =====================================================================
    // Write control
    // =====================================================================

    bool write_enable() noexcept {
        auto guard = lock();
        if (!send_command(opcode::write_enable)) return false;
        auto enabled = is_write_enabled();
        if (!enabled) return false;
        if (!*enabled) {
            m_last_error = err::write_not_enabled;
            return false;
        }
        return true;
    }

    bool write_enable_volatile_sr() noexcept {
        return send_command(opcode::write_enable_volatile_sr);
    }

    bool write_disable() noexcept {
        return send_command(opcode::write_disable);
    }

    // =====================================================================
    // Read
    // =====================================================================

    ///
    /// \brief Read data from flash (opcode 0x03).
    ///
    /// \param address Starting byte address (0..16 MiB - 1).
    /// \param rx      Caller's receive buffer; \c rx.size() bytes are
    ///                read starting at \p address.
    /// \return \c true on success.
    ///
    /// \details Reads are issued in chunks of \ref PAGE_SIZE_BYTES so
    ///          per-transaction stack usage is bounded; CS is asserted
    ///          fresh on each chunk with the next address. There is no
    ///          functional difference from a single huge read at the
    ///          flash side.
    ///
    bool read_data(uint32_t address,
                   sentinel::span<uint8_t> rx) noexcept {
        return read_chunked(opcode::read_data, /*dummy_bytes=*/0,
                            address, rx);
    }

    ///
    /// \brief Fast Read data from flash (opcode 0x0B).
    ///
    /// \details Identical to \ref read_data except one dummy clock byte
    ///          is sent after the address. At higher SPI clocks, this
    ///          opcode is required (0x03 is rated to ~50 MHz; 0x0B works
    ///          up to the chip's full clock rating).
    ///
    bool fast_read(uint32_t address,
                   sentinel::span<uint8_t> rx) noexcept {
        return read_chunked(opcode::fast_read, /*dummy_bytes=*/1,
                            address, rx);
    }

    // =====================================================================
    // Program
    // =====================================================================

    ///
    /// \brief Program a page (opcode 0x02).
    ///
    /// \details Programs up to \ref PAGE_SIZE_BYTES at \p address. The
    ///          payload must not cross a 256-byte page boundary (the
    ///          chip wraps within the page, which is almost always not
    ///          what the caller wants). Internally:
    ///          1. \ref write_enable
    ///          2. Send opcode + 3 address bytes + data in one
    ///             CS-asserted transaction
    ///          3. \ref wait_until_ready
    ///
    /// \param address Page-relative starting address.
    /// \param tx      Data to program (1..256 bytes).
    /// \return \c true on success.
    ///
    bool page_program(uint32_t address,
                      sentinel::span<const uint8_t> tx) noexcept {
        auto guard = lock();
        if (tx.empty() || tx.size() > PAGE_SIZE_BYTES) {
            m_last_error = err::invalid_argument;
            return false;
        }
        if (address + tx.size() > TOTAL_SIZE_BYTES) {
            m_last_error = err::invalid_argument;
            return false;
        }
        // Page boundary check.
        if ((address & (PAGE_SIZE_BYTES - 1u)) + tx.size() > PAGE_SIZE_BYTES) {
            m_last_error = err::invalid_argument;
            return false;
        }

        if (!write_enable()) return false;

        auto buffer = std::array<uint8_t, 4 + PAGE_SIZE_BYTES>{};
        buffer[0] = static_cast<uint8_t>(opcode::page_program);
        buffer[1] = static_cast<uint8_t>((address >> 16) & 0xFF);
        buffer[2] = static_cast<uint8_t>((address >> 8)  & 0xFF);
        buffer[3] = static_cast<uint8_t>(address & 0xFF);
        std::copy(tx.begin(), tx.end(), buffer.begin() + 4);

        if (!transact(buffer.data(), 4 + tx.size(), nullptr, 0)) {
            return false;
        }

        return wait_until_write_complete();
    }

    // =====================================================================
    // Erase
    // =====================================================================

    bool sector_erase_4kb(uint32_t address) noexcept {
        return erase_with_address(opcode::sector_erase_4kb, address,
                                  /*timeout_ms=*/2000);
    }
    bool block_erase_32kb(uint32_t address) noexcept {
        return erase_with_address(opcode::block_erase_32kb, address,
                                  /*timeout_ms=*/4000);
    }
    bool block_erase_64kb(uint32_t address) noexcept {
        return erase_with_address(opcode::block_erase_64kb, address,
                                  /*timeout_ms=*/4000);
    }

    ///
    /// \brief Erase the entire chip (opcode 0xC7).
    ///
    /// \details Datasheet quotes 25 s typical, 200 s maximum. The default
    ///          timeout here is 250 s so a worst-case device on a slow
    ///          bus does not spuriously time out.
    ///
    bool chip_erase() noexcept {
        auto guard = lock();
        if (!write_enable()) return false;
        if (!send_command(opcode::chip_erase)) return false;
        return wait_until_write_complete(/*timeout_ms=*/250000);
    }

    // =====================================================================
    // Suspend / Resume
    // =====================================================================

    bool erase_program_suspend() noexcept {
        return send_command(opcode::erase_program_suspend);
    }
    bool erase_program_resume() noexcept {
        return send_command(opcode::erase_program_resume);
    }

    // =====================================================================
    // Power / Reset
    // =====================================================================

    ///
    /// \brief Put the device into deep power-down (opcode 0xB9).
    ///
    /// \details After this call the device draws ~1 µA and ignores all
    ///          commands except \c 0xAB (Release Power-Down). Wake it up
    ///          via \ref release_power_down or
    ///          \ref release_power_down_device_id.
    ///
    bool power_down() noexcept {
        return send_command(opcode::power_down);
    }

    ///
    /// \brief Wake the device from deep power-down (opcode 0xAB, no
    ///        device-ID read).
    ///
    /// \details Datasheet specifies tRES1 ≈ 3 µs from CS-low to
    ///          device-ready. This call sends the opcode and immediately
    ///          deasserts CS; callers wanting the byte-readable
    ///          \c DEVICE_ID payload should use
    ///          \ref release_power_down_device_id instead.
    ///
    bool release_power_down() noexcept {
        return send_command(opcode::release_power_down);
    }

    ///
    /// \brief Software reset: Enable Reset (0x66) followed by Reset
    ///        Device (0x99).
    ///
    /// \details Per the datasheet, the device requires a back-to-back
    ///          0x66 + 0x99 sequence to reset. Either opcode alone is
    ///          ignored. After reset, callers should wait ~30 µs before
    ///          issuing the next command.
    ///
    bool software_reset() noexcept {
        auto guard = lock();
        if (!send_command(opcode::enable_reset))  return false;
        if (!send_command(opcode::reset_device))  return false;
        m_bus.delay(1); // generous; datasheet wants ~30 µs
        return true;
    }

    // =====================================================================
    // Security registers (3 × 256 B, individually erasable)
    // =====================================================================

    ///
    /// \brief Erase one security register (opcode 0x44).
    ///
    /// \param index Security register index, 1..3 (matches datasheet
    ///              numbering; mapped internally to address bytes
    ///              0x0010xx / 0x0020xx / 0x0030xx).
    ///
    bool erase_security_register(uint8_t index) noexcept {
        auto guard = lock();
        if (index < 1 || index > SECURITY_REGISTER_COUNT) {
            m_last_error = err::invalid_argument;
            return false;
        }
        auto addr = security_register_address(index, /*offset=*/0);

        if (!write_enable()) return false;
        if (!send_command_with_address(opcode::erase_security_register,
                                        addr)) {
            return false;
        }
        return wait_until_ready(/*timeout_ms=*/1000);
    }

    ///
    /// \brief Program one security register (opcode 0x42).
    ///
    bool program_security_register(uint8_t index, uint8_t offset,
                                    sentinel::span<const uint8_t> tx) noexcept {
        auto guard = lock();
        if (index < 1 || index > SECURITY_REGISTER_COUNT) {
            m_last_error = err::invalid_argument;
            return false;
        }
        if (tx.empty() || tx.size() > SECURITY_REGISTER_SIZE_BYTES) {
            m_last_error = err::invalid_argument;
            return false;
        }
        if (static_cast<uint32_t>(offset) + tx.size()
            > SECURITY_REGISTER_SIZE_BYTES) {
            m_last_error = err::invalid_argument;
            return false;
        }

        auto addr = security_register_address(index, offset);

        if (!write_enable()) return false;

        auto buffer =
            std::array<uint8_t, 4 + SECURITY_REGISTER_SIZE_BYTES>{};
        buffer[0] = static_cast<uint8_t>(opcode::program_security_register);
        buffer[1] = static_cast<uint8_t>((addr >> 16) & 0xFF);
        buffer[2] = static_cast<uint8_t>((addr >> 8)  & 0xFF);
        buffer[3] = static_cast<uint8_t>(addr & 0xFF);
        std::copy(tx.begin(), tx.end(), buffer.begin() + 4);

        if (!transact(buffer.data(), 4 + tx.size(), nullptr, 0)) {
            return false;
        }
        return wait_until_ready();
    }

    ///
    /// \brief Read one security register (opcode 0x48).
    ///
    bool read_security_register(uint8_t index, uint8_t offset,
                                 sentinel::span<uint8_t> rx) noexcept {
        if (index < 1 || index > SECURITY_REGISTER_COUNT) {
            m_last_error = err::invalid_argument;
            return false;
        }
        if (static_cast<uint32_t>(offset) + rx.size()
            > SECURITY_REGISTER_SIZE_BYTES) {
            m_last_error = err::invalid_argument;
            return false;
        }

        auto addr = security_register_address(index, offset);
        // Opcode + 3 addr bytes + 1 dummy byte, then data.
        return read_chunked(opcode::read_security_register,
                            /*dummy_bytes=*/1, addr, rx);
    }

    // =====================================================================
    // SFDP
    // =====================================================================

    ///
    /// \brief Read SFDP (Serial Flash Discoverable Parameters) region.
    ///
    /// \details Standard SFDP layout per JEDEC JESD216. Address space is
    ///          256 bytes; the typical caller starts at 0 and reads the
    ///          header to discover everything else.
    ///
    bool read_sfdp(uint32_t address,
                   sentinel::span<uint8_t> rx) noexcept {
        if (rx.empty()) return true;
        if (address + rx.size() > SFDP_SIZE_BYTES) {
            m_last_error = err::invalid_argument;
            return false;
        }
        return read_chunked(opcode::read_sfdp_register, /*dummy_bytes=*/1,
                            address, rx);
    }

    // =====================================================================
    // Low-level escape hatches
    // =====================================================================

    ///
    /// \brief Send a single-byte command with no address or payload.
    ///
    bool send_command(opcode cmd) noexcept {
        auto tx = std::array<uint8_t, 1>{static_cast<uint8_t>(cmd)};
        return transact(tx.data(), tx.size(), nullptr, 0);
    }

private:
    // =====================================================================
    // Device locking
    // =====================================================================

    ///
    /// \brief RAII guard that holds \ref m_device_mutex for its lifetime.
    ///
    /// \details A no-op when the driver was constructed without a mutex. The
    ///          mutex is recursive, so nested locked operations (e.g.
    ///          \c page_program taking the lock and then calling
    ///          \c write_enable, which also takes it) do not deadlock.
    ///
    class scoped_device_lock {
    public:
        explicit scoped_device_lock(SemaphoreHandle_t mutex) noexcept
            : m_mutex(mutex) {
            if (m_mutex != nullptr) {
                xSemaphoreTakeRecursive(m_mutex, portMAX_DELAY);
            }
        }
        ~scoped_device_lock() {
            if (m_mutex != nullptr) {
                xSemaphoreGiveRecursive(m_mutex);
            }
        }
        scoped_device_lock(const scoped_device_lock &)            = delete;
        scoped_device_lock &operator=(const scoped_device_lock &) = delete;

    private:
        SemaphoreHandle_t m_mutex;
    };

    /// Acquire the device lock for the current scope (guaranteed copy elision
    /// lets callers write \c auto guard = lock();).
    scoped_device_lock lock() const noexcept {
        return scoped_device_lock(m_device_mutex);
    }

    // =====================================================================
    // Internal helpers
    // =====================================================================

    ///
    /// \brief Send opcode + 3-byte address (no data phase).
    ///
    bool send_command_with_address(opcode cmd, uint32_t address) noexcept {
        auto tx = std::array<uint8_t, 4>{
            static_cast<uint8_t>(cmd),
            static_cast<uint8_t>((address >> 16) & 0xFF),
            static_cast<uint8_t>((address >> 8)  & 0xFF),
            static_cast<uint8_t>(address & 0xFF),
        };
        return transact(tx.data(), tx.size(), nullptr, 0);
    }

    ///
    /// \brief Read a single byte from a register command (opcode + 1 rx).
    ///
    std::optional<uint8_t>
    read_one_byte_register(opcode cmd) const noexcept {
        auto tx = std::array<uint8_t, 1>{static_cast<uint8_t>(cmd)};
        auto rx = std::array<uint8_t, 2>{}; // cmd echo + 1 data byte
        if (!transact(tx.data(), tx.size(), rx.data(), rx.size())) {
            return std::nullopt;
        }
        return rx[1];
    }

    ///
    /// \brief Common implementation behind the three write-status-reg
    ///        members.
    ///
    bool write_status_register(opcode cmd, uint8_t value,
                               bool volatile_only) noexcept {
        auto guard = lock();
        if (volatile_only) {
            if (!write_enable_volatile_sr()) return false;
        } else {
            if (!write_enable()) return false;
        }

        auto tx = std::array<uint8_t, 2>{
            static_cast<uint8_t>(cmd), value};
        if (!transact(tx.data(), tx.size(), nullptr, 0)) {
            return false;
        }
        // Non-volatile writes are NV-OTP-style and take ~10 ms.
        return wait_until_ready(/*timeout_ms=*/50);
    }

    ///
    /// \brief Common erase-command implementation: write-enable, send
    ///        opcode + address, wait for completion.
    ///
    bool erase_with_address(opcode cmd, uint32_t address,
                            uint32_t timeout_ms) noexcept {
        auto guard = lock();
        if (address >= TOTAL_SIZE_BYTES) {
            m_last_error = err::invalid_argument;
            return false;
        }
        if (!write_enable()) return false;
        if (!send_command_with_address(cmd, address)) return false;
        return wait_until_write_complete(timeout_ms);
    }

    ///
    /// \brief Read \p rx.size() bytes using a generic "opcode + 3-byte
    ///        address + N dummy bytes + data" template, chunking by
    ///        \ref PAGE_SIZE_BYTES.
    ///
    /// \details Each chunk re-issues opcode and address with an
    ///          incremented offset; this avoids needing a single
    ///          enormous scratch buffer for large reads while still
    ///          being correct (every chunk is one CS-asserted window
    ///          from the flash's perspective).
    ///
    bool read_chunked(opcode cmd, uint8_t dummy_bytes, uint32_t address,
                      sentinel::span<uint8_t> rx) noexcept {
        // Hold the lock across every chunk so another task's program/erase
        // cannot start mid-read and leave the device BUSY (which would make a
        // subsequent chunk read undefined data).
        auto guard = lock();
        if (rx.empty()) {
            m_last_error = err::ok;
            return true;
        }

        const auto prefix = static_cast<size_t>(1u + 3u + dummy_bytes);

        auto offset = size_t{0};
        while (offset < rx.size()) {
            auto const this_chunk = std::min(static_cast<size_t>(
                                                 PAGE_SIZE_BYTES),
                                             rx.size() - offset);
            auto const addr = address + static_cast<uint32_t>(offset);

            auto tx_buf = std::array<uint8_t, 8>{}; // up to opcode+3 addr+4 dummies
            tx_buf[0] = static_cast<uint8_t>(cmd);
            tx_buf[1] = static_cast<uint8_t>((addr >> 16) & 0xFF);
            tx_buf[2] = static_cast<uint8_t>((addr >> 8)  & 0xFF);
            tx_buf[3] = static_cast<uint8_t>(addr & 0xFF);
            // dummy bytes (tx_buf[4..]) already zero-initialized

            auto rx_buf =
                std::array<uint8_t, 8 + PAGE_SIZE_BYTES>{};

            if (!transact(tx_buf.data(), prefix,
                          rx_buf.data(), prefix + this_chunk)) {
                return false;
            }

            std::copy(rx_buf.data() + prefix,
                      rx_buf.data() + prefix + this_chunk,
                      rx.data() + offset);

            offset += this_chunk;
        }

        m_last_error = err::ok;
        return true;
    }

    ///
    /// \brief Single SPI transaction through the bound transport.
    ///
    /// \details One CS-asserted window. \c rx may be \c nullptr (write-
    ///          only) and \c rx_size may be 0; same for \c tx. Returns
    ///          \c true on transport success, \c false otherwise (after
    ///          updating \c m_last_error).
    ///
    bool transact(const uint8_t *tx, size_t tx_size,
                  uint8_t *rx, size_t rx_size) const noexcept {
        auto guard = lock();
        auto rc = cy_rslt_t{};

        if (rx_size > 0) {
            rc = m_bus.write_read(tx, tx_size, rx, rx_size);
        } else {
            rc = m_bus.write(tx, tx_size);
        }

        if (rc != CY_RSLT_SUCCESS) {
            m_last_error = err::transport_failure;
            return false;
        }
        m_last_error = err::ok;
        return true;
    }

    ///
    /// \brief Compose the 24-bit address that selects a particular
    ///        security register byte.
    ///
    /// \details Per datasheet, security register addresses are
    ///          \c 0x001000 (reg 1), \c 0x002000 (reg 2),
    ///          \c 0x003000 (reg 3), with the low 8 bits being the
    ///          byte offset within the 256-byte register.
    ///
    static constexpr uint32_t
    security_register_address(uint8_t index, uint8_t offset) noexcept {
        return (static_cast<uint32_t>(index) << 12) |
               static_cast<uint32_t>(offset);
    }

    Transport         &m_bus;                    ///< Non-owning bus ref.
    SemaphoreHandle_t  m_device_mutex;           ///< Optional device lock.
    mutable err        m_last_error{err::ok};    ///< Cached most-recent.
};

#endif /* SENTINEL_W25Q128_HPP */
