///
/// \file    sentinel_ram_record_store.hpp
/// \brief   RAM-backed circular record store (test double for record_store)
///
/// \details Provides \ref sentinel::ram_record_store, a drop-in alternative to
///          the flash-backed \ref sentinel::record_store that exposes the
///          **same duck-typed API** but keeps records in a caller-supplied RAM
///          buffer instead of on the W25Q128. It exists so the System Event Log
///          (firmware #34) and its tests can run without wearing out flash and
///          without needing a physical part on the bus.
///
///          Because \ref sentinel::diagnostics::system_event_log is templated
///          on its store type, the exact same log code runs over
///          \ref sentinel::record_store in the application and over
///          \ref sentinel::ram_record_store in the testbench.
///          This store therefore mirrors the flash store's observable contract:
///
///          - `bool initialize()`            — recover head/tail from backing
///          - `bool append(const RecordT&)`  — power-loss-safe two-phase write
///          - `bool read(uint32_t, RecordT*)`— indexed read in [tail, head)
///          - `uint32_t count() / capacity() / head_index() / tail_index()`
///          - `bool erase_all()`             — reset backing to empty
///          - `bool append_uncommitted_for_test(const RecordT&)` — torn write
///          - `err last_error()` / `bool initialized()`
///
///          === Backing layout (mirrors the flash slot header) ===
///
///          Records live in fixed-size slots inside the caller's byte buffer.
///          Each slot is `status(1) + reserved(3) + sequence(4) + payload`:
///
///          \code{.unparsed}
///          offset 0 : status   (1 byte)  0xFF empty / 0xA5 valid
///          offset 1 : reserved (3 bytes)
///          offset 4 : sequence (4 bytes) monotonic absolute record index
///          offset 8 : payload  (sizeof(RecordT) bytes)
///          \endcode
///
///          Unlike the flash store, RAM has no 4 KiB erase granularity, so a
///          slot is exactly `8 + sizeof(RecordT)` bytes (no power-of-two
///          padding) and on wrap the single oldest record is overwritten and
///          \c tail advances by one. The buffer being *caller-owned* is what
///          lets a test simulate a warm reboot: construct a fresh store over
///          the same buffer and call \ref
///          sentinel::ram_record_store::initialize() to re-derive head/tail
///          purely from the bytes already there — exactly as the flash store
///          recovers from on-flash state.
///
/// \author  galudino
/// \date    2026-06-28
/// \version 1.0 - RAM-backed record store test double (firmware #34)
///

#ifndef SENTINEL_RAM_RECORD_STORE_HPP
#define SENTINEL_RAM_RECORD_STORE_HPP

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace sentinel {

///
/// \brief RAM-backed circular record store mirroring \ref
/// sentinel::record_store.
///
/// \tparam RecordT The caller's fixed-size record payload. Must be trivially
///                 copyable and a multiple of 4 bytes (same constraints the
///                 flash store imposes, so a type that fits one fits both).
///
template <typename RecordT>
class ram_record_store {
    static_assert(std::is_trivially_copyable_v<RecordT>,
                  "RecordT must be trivially copyable");
    static_assert(sizeof(RecordT) % 4 == 0,
                  "RecordT must be a multiple of 4 bytes");

public:
    ///
    /// \brief Error codes for the most recent operation (see \ref last_error).
    ///
    enum class err : int8_t {
        ok = 0,
        invalid_argument = -2, ///< Bad buffer/index/argument.
        not_initialized = -3,  ///< Operation issued before \ref initialize().
        corrupt_record = -4,   ///< A slot expected valid had a bad status byte.
    };

    /// Per-slot header: status(1) + reserved(3) + sequence(4).
    static constexpr uint32_t HEADER_SIZE = 8u;

    static constexpr uint32_t OFFSET_STATUS = 0u;   ///< Status byte offset.
    static constexpr uint32_t OFFSET_SEQUENCE = 4u; ///< Sequence field offset.
    static constexpr uint32_t OFFSET_PAYLOAD = 8u;  ///< Payload field offset.

    /// Slot stride in bytes (no power-of-two padding needed in RAM).
    static constexpr uint32_t SLOT_SIZE =
        HEADER_SIZE + static_cast<uint32_t>(sizeof(RecordT));

    static constexpr uint8_t STATUS_EMPTY = 0xFFu; ///< Slot never written.
    static constexpr uint8_t STATUS_VALID =
        0xA5u; ///< Slot holds a valid record.

    ///
    /// \brief Construct a store over a caller-owned byte buffer.
    ///
    /// \param buffer      Backing storage. Must outlive the store. To simulate
    ///                    a warm reboot, construct a new store over the same
    ///                    buffer and call \ref initialize().
    /// \param buffer_size Size of \p buffer in bytes. Capacity is
    ///                    \c buffer_size / \ref SLOT_SIZE (>= 1 required).
    ///
    ram_record_store(uint8_t *buffer, uint32_t buffer_size) noexcept
        : m_buffer(buffer), m_capacity(buffer_size / SLOT_SIZE) {}

    ram_record_store(const ram_record_store &) = delete;
    ram_record_store &operator=(const ram_record_store &) = delete;
    /// \brief Move-construct from another instance (defaulted).
    ram_record_store(ram_record_store &&) noexcept = default;
    /// \brief Move-assign from another instance (defaulted).
    /// \return Reference to this instance.
    ram_record_store &operator=(ram_record_store &&) noexcept = default;

    /// \brief The error code recorded by the most recent failed operation.
    /// \return The most recent \ref err (\c err::ok if none).
    err last_error() const noexcept { return m_last_error; }

    /// \brief Total number of record slots the buffer can hold.
    /// \return \ref m_capacity.
    uint32_t capacity() const noexcept { return m_capacity; }
    /// \brief Number of valid records currently stored: \c head - \c tail.
    /// \return Current record count.
    uint32_t count() const noexcept { return m_head - m_tail; }
    /// \brief Absolute index where the next appended record will land.
    /// \return \ref m_head.
    uint32_t head_index() const noexcept { return m_head; }
    /// \brief Absolute index of the oldest still-valid record.
    /// \return \ref m_tail.
    uint32_t tail_index() const noexcept { return m_tail; }
    /// \brief Has \ref initialize() (or \ref erase_all()) run successfully?
    /// \return \c true if the store is ready for append/read.
    bool initialized() const noexcept { return m_initialized; }

    ///
    /// \brief Recover \c head / \c tail by scanning the backing buffer.
    ///
    /// \details Mirrors the flash store: head/tail are \c max(sequence)+1 and
    ///          \c min(sequence) over the valid slots, so recovery is correct
    ///          even after the log has wrapped. A buffer of all-0xFF (erased)
    ///          recovers as empty.
    ///
    /// \return \c true on success; \c false if the buffer is \c nullptr or
    ///         has zero capacity.
    ///
    bool initialize() noexcept {
        if (m_buffer == nullptr || m_capacity == 0u) {
            m_last_error = err::invalid_argument;
            return false;
        }

        auto have_any = false;
        auto min_seq = uint32_t{0};
        auto max_seq = uint32_t{0};

        for (auto slot = uint32_t{0}; slot < m_capacity; slot++) {
            const auto *s = slot_ptr(slot);
            if (s[OFFSET_STATUS] != STATUS_VALID) {
                continue;
            }
            auto seq = load_sequence(s);
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
    /// \brief Reset the backing buffer to empty (all slots erased).
    ///
    /// \return \c true on success; \c false if the buffer is \c nullptr or
    ///         has zero capacity.
    ///
    bool erase_all() noexcept {
        if (m_buffer == nullptr || m_capacity == 0u) {
            m_last_error = err::invalid_argument;
            return false;
        }
        std::memset(m_buffer, STATUS_EMPTY, m_capacity * SLOT_SIZE);
        m_head = 0u;
        m_tail = 0u;
        m_initialized = true;
        m_last_error = err::ok;
        return true;
    }

    ///
    /// \brief Append a record (two-phase: payload + sequence, then status).
    ///
    /// \details On wrap (head has lapped capacity) the slot being reused holds
    ///          the oldest record; overwriting it advances \c tail by one so
    ///          the oldest-overwritten-wins policy matches the flash store's
    ///          observable behaviour.
    ///
    /// \param record The record to store.
    /// \return \c true on success; \c false if not initialized.
    ///
    bool append(const RecordT &record) noexcept {
        if (!m_initialized) {
            m_last_error = err::not_initialized;
            return false;
        }
        const auto slot = m_head % m_capacity;

        if (m_head >= m_capacity) {
            const auto overwritten = m_head - m_capacity; // oldest in this slot
            if (m_tail <= overwritten) {
                m_tail = overwritten + 1u;
            }
        }

        write_payload(slot, m_head, record);
        slot_ptr(slot)[OFFSET_STATUS] = STATUS_VALID; // commit

        m_head++;
        m_last_error = err::ok;
        return true;
    }

    ///
    /// \brief Read the record at an absolute index in <tt>[tail, head)</tt>.
    ///
    /// \param index Absolute record index; must be in <tt>[tail, head)</tt>.
    /// \param out   Destination for the payload.
    /// \return \c true on success; \c false if not initialized, \p out is
    ///         \c nullptr, \p index is out of range, or the slot is corrupt.
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
        const auto *s = slot_ptr(index % m_capacity);
        if (s[OFFSET_STATUS] != STATUS_VALID) {
            m_last_error = err::corrupt_record;
            return false;
        }
        std::memcpy(out, s + OFFSET_PAYLOAD, sizeof(RecordT));
        m_last_error = err::ok;
        return true;
    }

    ///
    /// \brief TEST ONLY: write payload + sequence without committing status.
    ///
    /// \details Emulates a power loss between the payload write and the status
    ///          commit; \c head is not advanced. A following \ref initialize()
    ///          must skip the partial slot.
    ///
    /// \param record The record to write (payload only; status left \c 0xFF).
    /// \return \c true on success; \c false if not initialized.
    ///
    bool append_uncommitted_for_test(const RecordT &record) noexcept {
        if (!m_initialized) {
            m_last_error = err::not_initialized;
            return false;
        }
        write_payload(m_head % m_capacity, m_head, record);
        m_last_error = err::ok;
        return true;
    }

private:
    /// \brief Byte pointer to the start of a slot.
    /// \param slot Slot index (not an absolute record index).
    /// \return Pointer to the slot's header within \ref m_buffer.
    uint8_t *slot_ptr(uint32_t slot) noexcept {
        return m_buffer + slot * SLOT_SIZE;
    }
    /// \brief Byte pointer to the start of a slot (const overload).
    /// \param slot Slot index (not an absolute record index).
    /// \return Pointer to the slot's header within \ref m_buffer.
    const uint8_t *slot_ptr(uint32_t slot) const noexcept {
        return m_buffer + slot * SLOT_SIZE;
    }

    /// \brief Load a slot's 4-byte sequence field.
    /// \param slot Pointer to the slot's header (as returned by \ref slot_ptr).
    /// \return The slot's stored sequence number.
    static uint32_t load_sequence(const uint8_t *slot) noexcept {
        auto seq = uint32_t{0};
        std::memcpy(&seq, slot + OFFSET_SEQUENCE, sizeof(seq));
        return seq;
    }

    /// Write sequence + payload, leaving the status byte untouched (0xFF).
    /// \param slot     Slot index to write.
    /// \param sequence Absolute record index to stamp into the slot.
    /// \param record   Record payload to write.
    void write_payload(uint32_t slot, uint32_t sequence,
                       const RecordT &record) noexcept {
        auto *s = slot_ptr(slot);
        std::memcpy(s + OFFSET_SEQUENCE, &sequence, sizeof(sequence));
        std::memcpy(s + OFFSET_PAYLOAD, &record, sizeof(RecordT));
    }

    uint8_t *m_buffer;                 ///< Non-owning backing storage.
    uint32_t m_capacity;               ///< Slot count.
    uint32_t m_head{0};                ///< Next write index.
    uint32_t m_tail{0};                ///< Oldest valid index.
    bool m_initialized{false};         ///< Set once \ref initialize() runs.
    mutable err m_last_error{err::ok}; ///< Cached most-recent error.
};

} // namespace sentinel

#endif /* SENTINEL_RAM_RECORD_STORE_HPP */
