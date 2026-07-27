///
/// \file    sentinel_record_store.hpp
/// \brief   Flash-backed circular record store on the W25Q128 SPI NOR flash
///
/// \details Provides a reusable, templated \ref sentinel::record_store that
///          backs a contiguous region of a \ref sentinel::w25q128 device and
///          exposes a circular-append + indexed-read API. This is the storage
///          primitive consumed by the forthcoming System Event Log and
///          Periodic Snapshot Capture features (firmware #33).
///
///          It is deliberately **not** a filesystem. We need fixed-size
///          records, power-loss-safe append, bounded write amplification,
///          O(1) append + O(1) random read by index, and compatibility with
///          the W25Q128's 4 KiB sector / 256 B page geometry. That is a
///          circular log on flash, nothing more.
///
///          Public API design (matches \ref sentinel::w25q128 /
///          \ref sentinel::bme280 / \ref sentinel::ds3231):
///          - Action / mutator members return \c bool (\c true on success).
///          - The most recent error is exposed via
///            \ref sentinel::record_store::last_error(), typed as
///            \ref sentinel::record_store::err.
///
///          === On-flash record layout ===
///
///          Every record is stored in a fixed-size *slot*. The slot begins
///          with an 8-byte header followed by the caller's \c RecordT payload:
///
///          \code{.unparsed}
///          offset 0  : status   (1 byte)  0xFF empty / 0xA5 valid / 0x5A dead
///          offset 1  : reserved (3 bytes) padding -> 4-byte align
///          offset 4  : sequence (4 bytes) monotonic absolute record index
///          offset 8  : payload  (sizeof(RecordT) bytes)
///          \endcode
///
///          The slot size is the smallest power of two that holds the 8-byte
///          header plus the payload. Because every power of two <= 256 divides
///          both the 256 B page and the 4 KiB sector evenly, a slot never
///          straddles a page or sector boundary, the index->address map is a
///          single multiply, and an integral number of records fits per
///          sector. The cost is a little internal fragmentation; a future
///          version could pack records tightly with a straddle-skip rule.
///
///          === Why a sequence number (deviation from the #33 sketch) ===
///
///          The issue sketch recovered head/tail on boot by finding the
///          highest-index valid slot. That works only before the log wraps:
///          after wrap, every slot's status byte is identical (0xA5) and the
///          newest record is indistinguishable from the oldest by status
///          alone. We therefore store a 4-byte monotonic \c sequence in each
///          slot. On boot \ref sentinel::record_store::initialize() scans the
///          region, and head/tail
///          are recovered as \c max(sequence)+1 and \c min(sequence) over the
///          valid slots. CRC is still deferred to a future hardening pass, as
///          the issue notes — the status byte alone gives power-loss safety.
///
///          === Power-loss safety ===
///
///          Append is two NOR page-program operations within a single slot
///          (hence a single page):
///          1. Program \c sequence + \c payload, leaving \c status at 0xFF.
///          2. Program \c status := 0xA5 to commit.
///          NOR programming only clears bits (1->0), so writing the status
///          byte after the payload is legal. If power is lost between the two
///          steps the slot keeps \c status == 0xFF and is treated as empty on
///          the next \ref sentinel::record_store::initialize() scan — a
///          half-written record is never
///          read as valid.
///
///          === Concurrency ===
///
///          The store is single-writer by convention: every consumer routes
///          its appends through one task (e.g. the event-log task), so access
///          is naturally serialized. Wrap appends in a FreeRTOS mutex only if
///          real contention appears.
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - Flash-backed circular record store (firmware #33)
///

#ifndef SENTINEL_RECORD_STORE_HPP
#define SENTINEL_RECORD_STORE_HPP

#include "sentinel_span.hpp"
#include "sentinel_w25q128.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace sentinel {

template <typename RecordT, typename Transport>
class record_store;

} // namespace sentinel

///
/// \brief Flash-backed circular record store.
///
/// \tparam RecordT   The caller's fixed-size record payload type. Must be
///                   trivially copyable, a multiple of 4 bytes in size, and
///                   small enough that the 8-byte slot header plus the payload
///                   fit within one 256-byte page (i.e. sizeof <= 248).
/// \tparam Transport The SPI transport the backing \ref sentinel::w25q128 is
///                   parameterised over (e.g.
///                   \c sentinel::cyhal_spi_bus_transport).
///
template <typename RecordT, typename Transport>
class sentinel::record_store {
    static_assert(std::is_trivially_copyable_v<RecordT>,
                  "RecordT must be trivially copyable to live on flash");
    static_assert(sizeof(RecordT) % 4 == 0,
                  "RecordT must be a multiple of 4 bytes");
    static_assert(sizeof(RecordT) <= 248,
                  "8-byte slot header + RecordT must fit in one 256 B page");

    // Declared first so the static data member SLOT_SIZE (whose initializer
    // is parsed immediately, not in the complete-class context) can call it.
    /// \brief Smallest power of two that is >= \p value.
    /// \param value Value to round up.
    /// \return The smallest power of two >= \p value.
    static constexpr uint32_t next_power_of_two(uint32_t value) noexcept {
        auto p = uint32_t{1};
        while (p < value) {
            p <<= 1u;
        }
        return p;
    }

public:
    // =====================================================================
    // Types
    // =====================================================================

    /// Backing \ref sentinel::w25q128 driver type for this store's \c
    /// Transport.
    using flash_type = sentinel::w25q128<Transport>;

    ///
    /// \brief Error codes for the most recent operation (see \ref last_error).
    ///
    enum class err : int8_t {
        ok = 0,
        flash_failure = -1,    ///< An underlying W25Q128 operation failed.
        invalid_argument = -2, ///< Bad region geometry or out-of-range index.
        not_initialized = -3,  ///< Operation issued before \ref initialize().
        corrupt_record = -4,   ///< A slot expected valid had a bad status byte.
    };

    // =====================================================================
    // Slot geometry (all compile-time)
    // =====================================================================

    /// Backing flash sector size (smallest erase unit).
    static constexpr uint32_t SECTOR_SIZE = flash_type::SECTOR_SIZE_BYTES;

    /// Backing flash page size (programming unit / max single program).
    static constexpr uint32_t PAGE_SIZE = flash_type::PAGE_SIZE_BYTES;

    /// Per-slot header: status(1) + reserved(3) + sequence(4).
    static constexpr uint32_t HEADER_SIZE = 8u;

    static constexpr uint32_t OFFSET_STATUS = 0u;   ///< Status byte offset.
    static constexpr uint32_t OFFSET_SEQUENCE = 4u; ///< Sequence field offset.
    static constexpr uint32_t OFFSET_PAYLOAD = 8u;  ///< Payload field offset.

    /// Size of a single record slot on flash (power-of-two padded).
    static constexpr uint32_t SLOT_SIZE =
        next_power_of_two(HEADER_SIZE + static_cast<uint32_t>(sizeof(RecordT)));

    static_assert(SLOT_SIZE <= 256u,
                  "slot must fit within a single 256 B page");

    /// Records stored per 4 KiB sector (integral by power-of-two padding).
    static constexpr uint32_t RECORDS_PER_SECTOR = SECTOR_SIZE / SLOT_SIZE;

    // Status byte states. Flash erases to 0xFF, and NOR programming can only
    // clear bits, so EMPTY (all ones) -> VALID is a legal transition.
    static constexpr uint8_t STATUS_EMPTY = 0xFFu;   ///< Slot never written.
    static constexpr uint8_t STATUS_VALID = 0xA5u;   ///< Slot holds a valid
                                                     ///< record.
    static constexpr uint8_t STATUS_INVALID = 0x5Au; ///< Reserved for future
                                                     ///< logical delete.

    // =====================================================================
    // Construction
    // =====================================================================

    ///
    /// \brief Construct a store over a contiguous region of the flash.
    ///
    /// \details No bus traffic occurs during construction. Call
    ///          \ref initialize() before any append/read so head/tail are
    ///          recovered from the on-flash state.
    ///
    /// \param flash               Backing flash device. Must outlive the
    ///                            store.
    /// \param region_offset_bytes Byte offset of the region within the flash.
    ///                            Must be sector-aligned.
    /// \param region_size_bytes   Size of the region in bytes. Must be a
    ///                            non-zero multiple of \ref SECTOR_SIZE.
    ///
    record_store(flash_type &flash, uint32_t region_offset_bytes,
                 uint32_t region_size_bytes) noexcept
        : m_flash(flash), m_region_offset(region_offset_bytes),
          m_sector_count(region_size_bytes / SECTOR_SIZE),
          m_capacity(m_sector_count * RECORDS_PER_SECTOR) {}

    record_store(const record_store &) = delete;
    record_store &operator=(const record_store &) = delete;
    /// \brief Move-construct from another instance (defaulted).
    record_store(record_store &&) noexcept = default;
    /// \brief Move-assign from another instance (defaulted).
    /// \return Reference to this instance.
    record_store &operator=(record_store &&) noexcept = default;

    /// \brief The error code recorded by the most recent failed operation.
    /// \return The most recent \ref err (\c err::ok if none).
    err last_error() const noexcept { return m_last_error; }

    // =====================================================================
    // Geometry accessors
    // =====================================================================

    /// Total number of record slots the region can hold.
    /// \return \ref m_capacity.
    uint32_t capacity() const noexcept { return m_capacity; }

    /// Number of valid records currently stored: \c head - \c tail.
    /// \return Current record count.
    uint32_t count() const noexcept { return m_head - m_tail; }

    /// Absolute index where the next appended record will land.
    /// \return \ref m_head.
    uint32_t head_index() const noexcept { return m_head; }

    /// Absolute index of the oldest still-valid record.
    /// \return \ref m_tail.
    uint32_t tail_index() const noexcept { return m_tail; }

    /// \brief Has \ref initialize() (or \ref erase_all()) run successfully?
    /// \return \c true if the store is ready for append/read.
    bool initialized() const noexcept { return m_initialized; }

    // =====================================================================
    // Lifecycle
    // =====================================================================

    ///
    /// \brief Recover \c head / \c tail from the on-flash state.
    ///
    /// \details The authoritative recovery is a min/max \c sequence scan over
    ///          every valid slot (\ref initialize_full_scan()) — O(capacity),
    ///          i.e. ~8k slot reads per region on the production flash map, and
    ///          the dominant boot cost (~8.7 s each; firmware #49). That
    ///          exhaustive scan is only actually *required* once the log has
    ///          **wrapped**, because only then can the newest record sit
    ///          anywhere in the region. The common boot states are recovered
    ///          far more cheaply by first classifying the region from slot 0:
    ///
    ///          - **slot 0 valid, \c sequence == 0** — never wrapped (slot 0 is
    ///            only ever rewritten to a \c sequence \c >= \c capacity, and
    ///            that first happens on the first wrap). Slots \c [0,head) are
    ///            valid and \c [head,capacity) empty — one monotonic boundary,
    ///            so \c head is found by binary search in O(log capacity)
    ///            status reads and \c tail is 0 (\ref initialize_unwrapped()).
    ///          - **slot 0 empty** — either a truly blank region or a power
    ///            loss caught mid-recycle of sector 0 (sector 0 erased, later
    ///            sectors still valid). \ref region_is_blank() tells them apart
    ///            with a per-sector head probe (O(sector_count)); blank means
    ///            \c head == tail == 0, the transient falls through to the full
    ///            scan.
    ///          - **otherwise (slot 0 valid, \c sequence != 0)** — wrapped; the
    ///            full min/max scan is authoritative.
    ///
    ///          No on-flash format change. Safe to call again to re-scan (e.g.
    ///          to emulate a warm boot in tests).
    ///
    /// \return \c true on success; \c false on a transport failure.
    ///
    bool initialize() noexcept {
        auto header = std::array<uint8_t, HEADER_SIZE>{};
        if (!read_slot_header(0u, header.data())) {
            return false; // m_last_error set by helper
        }

        const auto status0 = header[OFFSET_STATUS];
        if (status0 == STATUS_VALID && load_sequence(header.data()) == 0u) {
            return initialize_unwrapped();
        }
        if (status0 == STATUS_EMPTY) {
            auto blank = false;
            if (!region_is_blank(&blank)) {
                return false;
            }
            if (blank) {
                m_tail = 0u;
                m_head = 0u;
                m_initialized = true;
                m_last_error = err::ok;
                return true;
            }
        }
        return initialize_full_scan();
    }

    ///
    /// \brief Erase every sector in the region and reset to empty.
    ///
    /// \return \c true on success; \c false on a flash erase failure.
    ///
    bool erase_all() noexcept {
        for (auto s = uint32_t{0}; s < m_sector_count; s++) {
            if (!m_flash.sector_erase_4kb(m_region_offset + s * SECTOR_SIZE)) {
                m_last_error = err::flash_failure;
                return false;
            }
        }
        m_head = 0u;
        m_tail = 0u;
        m_initialized = true;
        m_last_error = err::ok;
        return true;
    }

    // =====================================================================
    // Append / read
    // =====================================================================

    ///
    /// \brief Append a record (power-loss-safe, two-phase write).
    ///
    /// \details Writes \c sequence + \c payload first, then commits the
    ///          status byte. When the write position enters a fresh sector
    ///          that still holds older records, that sector is erased first
    ///          and \c tail advances past the records it destroyed
    ///          (oldest-overwritten-wins).
    ///
    /// \param record The record to store.
    /// \return \c true on success; \c false on error (see \ref last_error()).
    ///
    bool append(const RecordT &record) noexcept {
        if (!m_initialized) {
            m_last_error = err::not_initialized;
            return false;
        }

        const auto slot = m_head % m_capacity;

        if (!recycle_sector_if_needed(slot)) {
            return false;
        }

        if (!write_payload(slot, m_head, record)) {
            return false;
        }
        if (!commit_status(slot)) {
            return false;
        }

        m_head++;
        m_last_error = err::ok;
        return true;
    }

    ///
    /// \brief Read the record stored at an absolute index.
    ///
    /// \param index Absolute record index; must be in <tt>[tail, head)</tt>.
    /// \param out   Destination for the payload.
    /// \return \c true on success; \c false if \p index is out of range, the
    ///         slot is unexpectedly not valid, or a transport error occurs.
    ///
    bool read(uint32_t index, RecordT *out) const noexcept {
        if (!m_initialized) {
            m_last_error = err::not_initialized;
            return false;
        }
        if (out == nullptr || index < m_tail || index >= m_head) {
            m_last_error = err::invalid_argument;
            return false;
        }

        const auto slot = index % m_capacity;
        auto raw = std::array<uint8_t, HEADER_SIZE + sizeof(RecordT)>{};
        if (!m_flash.read_data(slot_address(slot),
                               sentinel::make_span(raw.data(), raw.size()))) {
            m_last_error = err::flash_failure;
            return false;
        }

        if (raw[OFFSET_STATUS] != STATUS_VALID) {
            m_last_error = err::corrupt_record;
            return false;
        }

        std::memcpy(out, raw.data() + OFFSET_PAYLOAD, sizeof(RecordT));
        m_last_error = err::ok;
        return true;
    }

    // =====================================================================
    // Test support
    // =====================================================================

    ///
    /// \brief TEST ONLY: write a record's payload without committing it.
    ///
    /// \details Performs only phase one of \ref append() — programs the
    ///          \c sequence and \c payload but leaves the status byte at
    ///          0xFF and does **not** advance \c head. This emulates a power
    ///          loss between the payload write and the status commit so a
    ///          test can verify that \ref initialize() skips the partial
    ///          record. After calling this, the region must be erased
    ///          (\ref erase_all()) before normal appends resume, since the
    ///          partial slot's bytes are no longer all-ones.
    ///
    /// \param record The record whose payload is written (uncommitted).
    /// \return \c true on success; \c false on error (see \ref last_error()).
    ///
    bool append_uncommitted_for_test(const RecordT &record) noexcept {
        if (!m_initialized) {
            m_last_error = err::not_initialized;
            return false;
        }
        const auto slot = m_head % m_capacity;
        if (!recycle_sector_if_needed(slot)) {
            return false;
        }
        return write_payload(slot, m_head, record);
    }

private:
    // =====================================================================
    // Address / serialization helpers
    // =====================================================================

    /// Physical byte address of a region-relative slot.
    ///
    /// Because \ref SLOT_SIZE divides \ref SECTOR_SIZE evenly, the per-sector
    /// layout is gapless and the address reduces to a single multiply.
    /// \param slot Region-relative slot index.
    /// \return Absolute byte address of the slot on flash.
    uint32_t slot_address(uint32_t slot) const noexcept {
        return m_region_offset + slot * SLOT_SIZE;
    }

    /// \brief Load a slot header's 4-byte sequence field.
    /// \param header Pointer to a slot's 8-byte header (as read by
    ///               \ref read_slot_header).
    /// \return The slot's stored sequence number.
    static uint32_t load_sequence(const uint8_t *header) noexcept {
        auto seq = uint32_t{0};
        std::memcpy(&seq, header + OFFSET_SEQUENCE, sizeof(seq));
        return seq;
    }

    /// Read a slot's 8-byte header into \p out (caller-owned, HEADER_SIZE).
    /// \param slot Region-relative slot index.
    /// \param out  Destination buffer, at least \ref HEADER_SIZE bytes.
    /// \return \c true on success; \c false on a transport failure.
    bool read_slot_header(uint32_t slot, uint8_t *out) noexcept {
        if (!m_flash.read_data(slot_address(slot),
                               sentinel::make_span(out, HEADER_SIZE))) {
            m_last_error = err::flash_failure;
            return false;
        }
        return true;
    }

    /// Read just a slot's 1-byte status field into \p out_status.
    /// \param slot       Region-relative slot index.
    /// \param out_status Destination for the 1-byte status field.
    /// \return \c true on success; \c false on a transport failure.
    bool read_slot_status(uint32_t slot, uint8_t *out_status) noexcept {
        if (!m_flash.read_data(slot_address(slot),
                               sentinel::make_span(out_status, 1u))) {
            m_last_error = err::flash_failure;
            return false;
        }
        return true;
    }

    // =====================================================================
    // Initialize internals (#49)
    // =====================================================================

    ///
    /// \brief Authoritative O(capacity) recovery: min/max \c sequence over
    ///        every valid slot.
    ///
    /// \details Reads each slot's header and locates the valid sequence range;
    ///          \c tail = \c min(sequence), \c head = \c max(sequence)+1. On a
    ///          region with no valid slot, \c head == tail == 0. Correct for
    ///          any on-flash state — including a wrapped log, where the newest
    ///          record can sit anywhere — so \ref initialize() falls back here
    ///          whenever the cheaper classified paths do not apply.
    ///
    /// \return \c true on success; \c false on a transport failure.
    ///
    bool initialize_full_scan() noexcept {
        auto have_any = false;
        auto min_seq = uint32_t{0};
        auto max_seq = uint32_t{0};

        for (auto slot = uint32_t{0}; slot < m_capacity; slot++) {
            auto header = std::array<uint8_t, HEADER_SIZE>{};
            if (!read_slot_header(slot, header.data())) {
                return false;
            }

            if (header[OFFSET_STATUS] != STATUS_VALID) {
                continue;
            }

            auto seq = load_sequence(header.data());
            if (!have_any) {
                min_seq = seq;
                max_seq = seq;
                have_any = true;
            } else {
                if (seq < min_seq) {
                    min_seq = seq;
                }
                if (seq > max_seq) {
                    max_seq = seq;
                }
            }
        }

        if (have_any) {
            m_tail = min_seq;
            m_head = max_seq + 1u;
        } else {
            m_tail = 0u;
            m_head = 0u;
        }

        m_initialized = true;
        m_last_error = err::ok;
        return true;
    }

    ///
    /// \brief Recover a never-wrapped region: binary-search the valid/empty
    ///        boundary for \c head; \c tail is 0.
    ///
    /// \details Precondition (guaranteed by the slot-0 classification in
    ///          \ref initialize()): slots \c [0,head) are \c STATUS_VALID and
    ///          \c [head,capacity) are \c STATUS_EMPTY — a single monotonic
    ///          transition. Probes only the 1-byte status field per step, so
    ///          recovery is O(log capacity) reads. A region filled exactly to
    ///          \c capacity but not yet wrapped has no empty slot, so the
    ///          search yields \c head == \c capacity.
    ///
    /// \return \c true on success; \c false on a transport failure.
    ///
    bool initialize_unwrapped() noexcept {
        auto lo = uint32_t{0};
        auto hi = m_capacity; // first non-valid slot lies in [lo, hi]
        while (lo < hi) {
            const auto mid = lo + (hi - lo) / 2u;
            auto status = uint8_t{};
            if (!read_slot_status(mid, &status)) {
                return false;
            }
            if (status == STATUS_VALID) {
                lo = mid + 1u;
            } else {
                hi = mid;
            }
        }

        m_tail = 0u;
        m_head = lo;
        m_initialized = true;
        m_last_error = err::ok;
        return true;
    }

    ///
    /// \brief Decide whether the region holds no valid record at all.
    ///
    /// \details Only called when slot 0 reads \c STATUS_EMPTY, to tell a truly
    ///          blank region from a power loss caught mid-recycle of sector 0
    ///          (sector 0 erased, later sectors still valid). Records fill each
    ///          sector strictly first-slot-first and a sector is erased as a
    ///          unit, so a sector holding any valid record has a valid first
    ///          slot; probing every sector's first slot therefore settles
    ///          blankness in \c sector_count reads (128 for a 512 KiB region)
    ///          instead of a full O(capacity) scan.
    ///
    /// \param out_blank Set to \c true iff no sector-head slot is valid.
    /// \return \c true on success; \c false on a transport failure.
    ///
    bool region_is_blank(bool *out_blank) noexcept {
        for (auto s = uint32_t{0}; s < m_sector_count; s++) {
            auto status = uint8_t{};
            if (!read_slot_status(s * RECORDS_PER_SECTOR, &status)) {
                return false;
            }
            if (status == STATUS_VALID) {
                *out_blank = false;
                return true;
            }
        }
        *out_blank = true;
        return true;
    }

    // =====================================================================
    // Append internals
    // =====================================================================

    ///
    /// \brief Erase the sector a slot belongs to if we are about to start
    ///        writing it and it still holds older records.
    ///
    /// \details Only the first slot of a sector triggers a potential erase.
    ///          Since records are written sequentially, a first slot reading
    ///          \c STATUS_EMPTY means the whole sector is already blank and no
    ///          erase is needed (the common first-lap case). Otherwise the
    ///          sector is erased and \c tail advances past the records it
    ///          destroyed.
    ///
    /// \param slot Region-relative slot about to be written.
    /// \return \c true on success; \c false on a flash erase failure.
    ///
    bool recycle_sector_if_needed(uint32_t slot) noexcept {
        if (slot % RECORDS_PER_SECTOR != 0u) {
            return true; // not a sector boundary; nothing to recycle
        }

        auto status = std::array<uint8_t, 1>{};
        if (!m_flash.read_data(
                slot_address(slot),
                sentinel::make_span(status.data(), status.size()))) {
            m_last_error = err::flash_failure;
            return false;
        }

        if (status[0] == STATUS_EMPTY) {
            return true; // sector already blank
        }

        const auto sector_base = slot_address(slot);
        if (!m_flash.sector_erase_4kb(sector_base)) {
            m_last_error = err::flash_failure;
            return false;
        }

        // The slots we just erased held the records one full lap behind the
        // current head. Advance tail past them if it pointed inside. Only
        // possible once the log has wrapped (head >= capacity); the guard
        // also prevents unsigned underflow of (head - capacity).
        if (m_head >= m_capacity) {
            const auto recycled_oldest = m_head - m_capacity;
            const auto recycled_end = recycled_oldest + RECORDS_PER_SECTOR;
            if (m_tail < recycled_end) {
                m_tail = recycled_end;
            }
        }
        return true;
    }

    /// Phase one: program sequence + payload, leaving status at 0xFF.
    /// \param slot     Region-relative slot to write.
    /// \param sequence Absolute record index to stamp into the slot.
    /// \param record   Record payload to write.
    /// \return \c true on success; \c false on a flash program failure.
    bool write_payload(uint32_t slot, uint32_t sequence,
                       const RecordT &record) noexcept {
        auto buffer = std::array<uint8_t, 4 + sizeof(RecordT)>{};
        std::memcpy(buffer.data(), &sequence, sizeof(sequence));
        std::memcpy(buffer.data() + 4, &record, sizeof(RecordT));

        const auto addr = slot_address(slot) + OFFSET_SEQUENCE;
        if (!m_flash.page_program(
                addr, sentinel::make_cspan(buffer.data(), buffer.size()))) {
            m_last_error = err::flash_failure;
            return false;
        }
        return true;
    }

    /// Phase two: program the status byte to commit the record.
    /// \param slot Region-relative slot to commit.
    /// \return \c true on success; \c false on a flash program failure.
    bool commit_status(uint32_t slot) noexcept {
        auto status = std::array<uint8_t, 1>{STATUS_VALID};
        const auto addr = slot_address(slot) + OFFSET_STATUS;
        if (!m_flash.page_program(
                addr, sentinel::make_cspan(status.data(), status.size()))) {
            m_last_error = err::flash_failure;
            return false;
        }
        return true;
    }

    // =====================================================================
    // State
    // =====================================================================

    flash_type &m_flash;      ///< Non-owning backing flash.
    uint32_t m_region_offset; ///< Region base byte offset.
    uint32_t m_region_size;   ///< Region size in bytes.
    uint32_t m_sector_count;  ///< Sectors in the region.
    uint32_t m_capacity;      ///< Slot count in the region.

    uint32_t m_head{0};                ///< Next write index.
    uint32_t m_tail{0};                ///< Oldest valid index.
    bool m_initialized{false};         ///< Set once \ref initialize() runs.
    mutable err m_last_error{err::ok}; ///< Cached most-recent error.
};

#endif /* SENTINEL_RECORD_STORE_HPP */
