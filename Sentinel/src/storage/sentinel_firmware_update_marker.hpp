///
/// \file    sentinel_firmware_update_marker.hpp
/// \brief   Durable pending-OTA-update marker on the W25Q128 (#81)
///
/// \details A truthful \c firmware_update_completed (0x05) and
///          \c firmware_reverted (0x07) can only be known at the \b next boot —
///          after the post-download reset and the MCUBoot swap. Logging at
///          \c OTA_COMPLETE (pre-reset) would conflate "download staged" with
///          "new image actually booted". This marker is the small durable
///          breadcrumb that carries an in-flight update across the reset so the
///          boot-time swap-confirm detector (#38 boot orchestrator) can emit
///          the two post-swap events honestly.
///
///          \b Storage. One fixed
///          \ref sentinel::storage::firmware_update_marker struct lives in a
///          dedicated 4 KiB sector on the W25Q128 — \b outside the circular
///          \ref sentinel::record_store (which would otherwise rotate it away
///          or conflate it with log records). Write == erase-sector +
///          page-program; clear == erase-sector; presence == the magic reads
///          back. The struct is 16 bytes, so a single page program (<= 256 B,
///          within one 256 B page) suffices.
///
///          \b Lifecycle (coordinated with #83/#84/#85/#86):
///          1. At OTA PREPARE the BLE context \c set()s the marker with the
///             running version (full 16-bit build) as \c from.
///          2. A \e failed OTA (signal loss / verify fail / agent-start fail)
///             already logs \c firmware_update_failed and reboots via
///             \c ota_fail_and_reset — which \c clear()s this marker first, so
///             a failure is never re-counted as a revert at the next boot.
///          3. A user \b cancel (ABORT) \c clear()s it too — nothing
///             is pending.
///          4. The \b success path marks the marker staged
///             (\c mark_staged, #71) and reboots via the OTA confirmation
///             handler
///             (\c NVIC_SystemReset on \c OTA_COMPLETE) \e without clearing it,
///             so the marker survives exactly the swap-triggering reset.
///          5. An unplanned reset (power cut, crash) mid-download leaves
///             the marker in its \e downloading stage (#71).
///          6. The next boot \c load()s it, resolves completed / reverted /
///             interrupted from the running version and the stage
///             (\ref sentinel::diagnostics::resolve_pending_update), records
///             the event, and \c clear()s the marker.
///
///          \b Revert-detection limit. A MCUBoot revert boots the \e old image;
///          it can emit \c firmware_reverted only if that old image also
///          carries this detector (#81+). A revert to a pre-#81 image cannot
///          self-report — acceptable, and documented here.
///
/// \author  galudino
/// \date    2026-11-13
/// \version 1.0 - Initial pending-update marker store (#81)
///

#ifndef SENTINEL_FIRMWARE_UPDATE_MARKER_HPP
#define SENTINEL_FIRMWARE_UPDATE_MARKER_HPP

#include "sentinel_span.hpp"
#include "sentinel_w25q128.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace sentinel::storage {

/// \brief Marker \c stage while the image is still downloading (#71). Written
///        as 0xFF (no bits cleared) so it can later become
///        \ref sentinel::storage::kMarkerStageStaged in place.
inline constexpr uint8_t kMarkerStageDownloading = 0xFFu;

/// \brief Marker \c stage once the downloaded image verified and is staged for
///        MCUBoot (#71). 0xFF → 0x7F only clears a bit: a 1-byte NOR program,
///        no sector erase — cheap enough for the BLE callback.
inline constexpr uint8_t kMarkerStageStaged = 0x7Fu;

/// \brief Marker \c stage of a marker written before #71 (the byte was zeroed
///        reserved space): download state unknown, resolved exactly as before.
inline constexpr uint8_t kMarkerStageLegacy = 0x00u;

///
/// \brief On-flash record describing an OTA update that is in flight across a
///        reset (#81).
///
/// \details Trivially copyable, fixed 16-byte layout so it lives directly on
///          NOR flash. \c from_* hold the four components of the version that
///          was running when the update was attempted. The 16-bit build is
///          split into \c from_build (low byte) and \c from_build_hi (#86, in
///          what was the zeroed reserved word — a marker written before #86
///          reads \c 0 there and reassembles to its old value).
///
struct firmware_update_marker {
    uint32_t magic; ///< \ref firmware_update_marker_store::kMarkerMagic when
                    ///< present.
    uint8_t from_major;      ///< Running major at attempt.
    uint8_t from_minor;      ///< Running minor at attempt.
    uint8_t from_patch;      ///< Running patch at attempt.
    uint8_t from_build;      ///< Running build, low byte, at attempt.
    uint32_t attempted_unix; ///< Unix seconds at attempt (0 if the RTC was not
                             ///< ready).
    uint8_t from_build_hi;   ///< Running build, high byte, at attempt (#86).
    uint8_t stage;           ///< Download stage (#71): \ref
                             ///< sentinel::storage::kMarkerStageDownloading,
                   ///< \ref sentinel::storage::kMarkerStageStaged or \ref
                   ///< sentinel::storage::kMarkerStageLegacy.
    uint8_t reserved[2]; ///< Reserved / padding for a future field or CRC.

    /// \brief The full 16-bit build at attempt (#86).
    /// \return <tt>(from_build_hi << 8) | from_build</tt>.
    constexpr uint16_t from_build_full() const noexcept {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(from_build_hi) << 8u) | from_build);
    }

    /// \brief Did the download finish (or is that unknown, pre-#71)?
    /// \return \c false only when the marker says the download was still in
    ///         progress — i.e. the reset that followed interrupted it.
    constexpr bool download_finished() const noexcept {
        return stage != kMarkerStageDownloading;
    }
};

static_assert(sizeof(firmware_update_marker) == 16,
              "firmware_update_marker must be a fixed 16-byte on-flash record");
static_assert(
    std::is_trivially_copyable_v<firmware_update_marker>,
    "firmware_update_marker must be trivially copyable to live on flash");

///
/// \brief Read/write the pending-update marker in its own W25Q128 sector (#81).
///
/// \tparam Transport SPI bus transport the backing \ref sentinel::w25q128 uses.
///
/// \details Non-owning over the shared flash driver; all three operations go
///          through it, so they are serialized by the flash device mutex along
///          with the record stores. The region is one 4 KiB sector; \ref set
///          erases it then programs the struct, \ref clear erases it, and
///          \ref load reads it back and validates \ref kMarkerMagic.
///
template <typename Transport>
class firmware_update_marker_store {
public:
    using flash_type = sentinel::w25q128<Transport>; ///< Backing flash driver.

    /// Marker-present sentinel programmed into \c magic (distinct from the
    /// 0xFFFFFFFF erased state and a 0x00000000 all-zero slot).
    static constexpr uint32_t kMarkerMagic = 0x53554D31u; // 'S''U''M''1'

    /// Byte offset of \c stage within the on-flash record (#71).
    static constexpr uint32_t kStageOffset =
        offsetof(firmware_update_marker, stage);

    ///
    /// \brief Bind the store to a flash region.
    /// \param flash         Backing W25Q128 (borrowed; must outlive this).
    /// \param region_offset Byte offset of the dedicated marker sector.
    ///
    firmware_update_marker_store(flash_type &flash,
                                 uint32_t region_offset) noexcept
        : m_flash(flash), m_offset(region_offset) {}

    ///
    /// \brief Persist a pending-update marker (erase the sector, then program).
    /// \param from_major Running major at attempt.
    /// \param from_minor Running minor at attempt.
    /// \param from_patch Running patch at attempt.
    /// \param from_build Running build (full 16 bits, #86) at attempt.
    /// \param attempted_unix Unix seconds at attempt (0 if unknown).
    /// \return \c true if the sector was erased and programmed successfully.
    ///
    bool set(uint8_t from_major, uint8_t from_minor, uint8_t from_patch,
             uint16_t from_build, uint32_t attempted_unix) noexcept {
        if (!m_flash.sector_erase_4kb(m_offset)) {
            return false;
        }

        auto rec = firmware_update_marker{};
        rec.magic = kMarkerMagic;
        rec.from_major = from_major;
        rec.from_minor = from_minor;
        rec.from_patch = from_patch;
        rec.from_build = static_cast<uint8_t>(from_build);
        rec.from_build_hi = static_cast<uint8_t>(from_build >> 8u);
        rec.stage = kMarkerStageDownloading;
        rec.attempted_unix = attempted_unix;

        std::array<uint8_t, sizeof(firmware_update_marker)> buf{};
        std::memcpy(buf.data(), &rec, sizeof(rec));
        return m_flash.page_program(
            m_offset, sentinel::make_cspan(buf.data(), buf.size()));
    }

    ///
    /// \brief Read the marker back if one is present.
    /// \param out Filled only when a valid marker (matching magic) is found.
    /// \return \c true if a valid marker was read; \c false if absent or on a
    ///         flash read error.
    ///
    bool load(firmware_update_marker &out) noexcept {
        std::array<uint8_t, sizeof(firmware_update_marker)> buf{};
        if (!m_flash.read_data(
                m_offset, sentinel::span<uint8_t>(buf.data(), buf.size()))) {
            return false;
        }

        auto rec = firmware_update_marker{};
        std::memcpy(&rec, buf.data(), sizeof(rec));
        if (rec.magic != kMarkerMagic) {
            return false;
        }

        out = rec;
        return true;
    }

    ///
    /// \brief Record that the download finished and the image is staged (#71).
    ///
    /// \details Programs \c stage from \ref
    /// sentinel::storage::kMarkerStageDownloading to
    ///          \ref sentinel::storage::kMarkerStageStaged in place (one byte,
    ///          bits only cleared — no erase). A no-op if no marker is present.
    ///
    /// \return \c true if the stage byte was programmed (or no marker exists).
    ///
    bool mark_staged() noexcept {
        auto rec = firmware_update_marker{};
        if (!load(rec)) {
            return true; // nothing pending to mark
        }
        const std::array<uint8_t, 1> stage{kMarkerStageStaged};
        return m_flash.page_program(
            m_offset + kStageOffset,
            sentinel::make_cspan(stage.data(), stage.size()));
    }

    ///
    /// \brief Erase the marker sector, clearing any pending marker.
    /// \return \c true on success.
    ///
    bool clear() noexcept { return m_flash.sector_erase_4kb(m_offset); }

private:
    flash_type &m_flash; ///< Non-owning backing flash.
    uint32_t m_offset;   ///< Absolute byte offset of the marker sector.
};

} // namespace sentinel::storage

#endif // SENTINEL_FIRMWARE_UPDATE_MARKER_HPP
