///
/// \file    sentinel_ring_buffer.hpp
/// \brief   Lock-free circular buffer for interrupt-safe data exchange
///
/// \details This header provides a lock-free ring buffer implementation
/// suitable
///          for single-producer single-consumer scenarios, particularly for
///          interrupt service routines. The capacity must be a power of two
///          to enable efficient modulo operations using bitwise AND.
///
/// \author  galudino
/// \date    2021-2024
/// \version 1.0 - Ring buffer container implementation
///

#ifndef SENTINEL_RING_BUFFER_HPP
#define SENTINEL_RING_BUFFER_HPP

#include <array>
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace sentinel {

///
/// \brief Lock-free circular buffer for ISR-safe data exchange
///
/// \details A ring buffer (circular buffer) that allows lock-free
///          single-producer single-consumer access. Designed for use in
///          interrupt service routines where data is produced in the ISR
///          and consumed in the main thread. The capacity must be a power
///          of two to enable efficient wraparound using bitwise operations.
///
/// \tparam T Element type
/// \tparam Capacity Buffer capacity (must be a power of two, minimum 2)
///
template <typename T, size_t Capacity = 256>
class ring_buffer_ {
public:
    using value_type = T;              ///< Element type stored in the buffer.
    using size_type = size_t;          ///< Type used for sizes and indices.
    using reference = T &;             ///< Mutable reference to an element.
    using const_reference = const T &; ///< Const reference to an element.
    using rvalue_reference = T &&;     ///< Rvalue reference to an element.

    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

    ///
    /// \brief Default constructor
    ///
    constexpr ring_buffer_() noexcept = default;

    ///
    /// \brief Get maximum capacity of the buffer
    ///
    /// \return Maximum number of elements the buffer can hold
    ///
    constexpr size_type capacity() const noexcept { return Capacity; }

    ///
    /// \brief Check if buffer is empty
    ///
    /// \return true if no elements are in the buffer
    ///
    constexpr bool empty() const noexcept { return m_head == m_tail; }

    ///
    /// \brief Check if buffer is full
    ///
    /// \return true if buffer is at maximum capacity
    ///
    constexpr bool full() const noexcept { return next(m_head) == m_tail; }

    ///
    /// \brief Get number of elements currently in the buffer
    ///
    /// \return Number of elements
    ///
    constexpr size_type size() const noexcept {
        return (m_head - m_tail) & (Capacity - 1);
    }

    ///
    /// \brief Get number of available elements (alias for size())
    ///
    /// \return Number of elements
    ///
    constexpr size_type available() const noexcept { return size(); }

    ///
    /// \brief Access oldest element without removing it
    ///
    /// \return Reference to the oldest element
    ///
    reference front() noexcept {
        assert(!empty());
        return m_store[m_tail];
    }

    ///
    /// \brief Access oldest element without removing it (const version)
    ///
    /// \return Const reference to the oldest element
    ///
    const_reference front() const noexcept {
        assert(!empty());
        return m_store[m_tail];
    }

    ///
    /// \brief Access newest element without removing it
    ///
    /// \return Reference to the newest element
    ///
    reference back() noexcept {
        assert(!empty());
        return m_store[prev(m_head)];
    }

    ///
    /// \brief Access newest element without removing it (const version)
    ///
    /// \return Const reference to the newest element
    ///
    const_reference back() const noexcept {
        assert(!empty());
        return m_store[prev(m_head)];
    }

    ///
    /// \brief Try to push element into buffer
    ///
    /// \param v Element to push (by const reference)
    /// \return true if element was pushed, false if buffer is full
    ///
    bool try_push(const_reference v) noexcept {
        if (full()) {
            return false;
        }

        m_store[m_head] = v;
        m_head = next(m_head);

        return true;
    }

    ///
    /// \brief Try to push element into buffer (move version)
    ///
    /// \param v Element to push (by rvalue reference)
    /// \return true if element was pushed, false if buffer is full
    ///
    bool try_push(rvalue_reference v) noexcept(
        std::is_nothrow_move_assignable<T>::value) {
        if (full()) {
            return false;
        }

        m_store[m_head] = std::move(v);
        m_head = next(m_head);

        return true;
    }

    ///
    /// \brief Push element, overwriting oldest if buffer is full
    ///
    /// \param v Element to push (by const reference)
    ///
    void push_overwrite(const_reference v) noexcept {
        m_store[m_head] = v;
        const auto next_head = next(m_head);

        if (next_head == m_tail) {
            m_tail = next(m_tail);
        } // drop oldest

        m_head = next_head;
    }

    ///
    /// \brief Push element, overwriting oldest if buffer is full (move version)
    ///
    /// \param v Element to push (by rvalue reference)
    ///
    void push_overwrite(rvalue_reference v) noexcept(
        std::is_nothrow_move_assignable<T>::value) {
        m_store[m_head] = std::move(v);
        const auto next_head = next(m_head);

        if (next_head == m_tail) {
            m_tail = next(m_tail);
        }

        m_head = next_head;
    }

    ///
    /// \brief Try to pop element from buffer
    ///
    /// \param out Reference to receive the popped element
    /// \return true if element was popped, false if buffer is empty
    ///
    bool try_pop(reference out) noexcept {
        if (empty()) {
            return false;
        }

        out = m_store[m_tail];
        m_tail = next(m_tail);
        return true;
    }

    ///
    /// \brief Discard oldest element without copying it
    ///
    /// \return true if element was discarded, false if buffer is empty
    ///
    bool drop_front() noexcept {
        if (empty()) {
            return false;
        }

        m_tail = next(m_tail);
        return true;
    }

    ///
    /// \brief Peek at oldest element without removing it
    ///
    /// \param out Reference to receive copy of the oldest element
    /// \return true if element was copied, false if buffer is empty
    ///
    bool peek_front(reference out) const noexcept {
        if (empty()) {
            return false;
        }

        out = m_store[m_tail];
        return true;
    }

    ///
    /// \brief Clear all elements from buffer
    ///
    /// \details Resets head and tail indices; elements remain in memory
    ///          but are considered invalid
    ///
    void clear() noexcept { m_head = m_tail = 0; }

private:
    ///
    /// \brief Calculate next index with wraparound
    ///
    /// \param i Current index
    /// \return Next index (wraps to 0 at capacity)
    ///
    static constexpr size_type next(size_type i) noexcept {
        return (i + 1) & (Capacity - 1);
    }

    ///
    /// \brief Calculate previous index with wraparound
    ///
    /// \param i Current index
    /// \return Previous index (wraps to capacity-1 at 0)
    ///
    static constexpr size_type prev(size_type i) noexcept {
        return (i - 1) & (Capacity - 1);
    }

    // Accessed from ISR and task; keep them volatile to prevent reorder/merge
    volatile size_type m_head{0}; ///< Write index (producer)
    volatile size_type m_tail{0}; ///< Read index (consumer)

    std::array<value_type, Capacity> m_store{}; ///< Element storage
};

} // namespace sentinel

#endif // SENTINEL_RING_BUFFER_HPP
