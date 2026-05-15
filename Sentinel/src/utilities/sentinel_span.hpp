///
/// \file    sentinel_span.hpp
/// \brief   Lightweight non-owning view over contiguous sequences
///
/// \details This header provides a C++11-compatible span implementation that
///          offers a non-owning view over contiguous data sequences. It serves
///          as a backport of C++20's std::span for use in embedded systems,
///          providing type-safe access to arrays, containers, and raw memory
///          without ownership semantics.
///
/// \author  galudino
/// \date    2021-2024
/// \version 1.0 - Span container implementation
///

#ifndef SENTINEL_SPAN_HPP
#define SENTINEL_SPAN_HPP

#include <array>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace sentinel {

///
/// \brief Non-owning view over a contiguous sequence of elements
///
/// \details A span represents a view over a contiguous sequence of objects
///          without owning the underlying memory. It provides a safe and
///          efficient way to pass arrays and containers to functions without
///          copying. This implementation is compatible with C++11 and provides
///          similar functionality to C++20's std::span.
///
/// \tparam T Element type (may be const-qualified)
///
template <typename T>
class span {
public:
    using element_type = T;
    using value_type = typename std::remove_cv<T>::type;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = T *;
    using const_pointer = const T *;
    using reference = T &;
    using const_reference = const T &;
    using iterator = T *;
    using const_iterator = const T *;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    ///
    /// \brief Default constructor creates an empty span
    ///
    constexpr span() noexcept : m_data(nullptr), m_size(0) {}

    ///
    /// \brief Construct span from pointer and size
    ///
    /// \param data Pointer to the first element
    /// \param size Number of elements in the sequence
    ///
    constexpr span(pointer data, size_type size) noexcept
        : m_data(data), m_size(size) {}

    ///
    /// \brief Construct span from pointer range
    ///
    /// \param first Pointer to the first element
    /// \param last Pointer one past the last element
    ///
    constexpr span(pointer first, pointer last) noexcept
        : m_data(first), m_size(static_cast<size_type>(last - first)) {}

    ///
    /// \brief Construct span from C-style array
    ///
    /// \tparam N Array size
    /// \param arr Reference to array of N elements
    ///
    template <size_type N>
    constexpr span(T (&arr)[N]) noexcept : m_data(arr), m_size(N) {}

    ///
    /// \brief Construct const span from const C-style array
    ///
    /// \tparam U Element type (must be convertible to const T)
    /// \tparam N Array size
    /// \param arr Reference to const array of N elements
    ///
    template <
        typename U, size_type N,
        typename = typename std::enable_if<
            std::is_const<T>::value && std::is_same<const U, T>::value>::type>
    constexpr span(const U (&arr)[N]) noexcept : m_data(arr), m_size(N) {}

    ///
    /// \brief Construct span from std::array
    ///
    /// \tparam U Element type (must match value_type)
    /// \tparam N Array size
    /// \param a Reference to std::array
    ///
    template <typename U, size_type N,
              typename = typename std::enable_if<
                  std::is_same<U, value_type>::value>::type>
    constexpr span(std::array<U, N> &a) noexcept
        : m_data(a.data()), m_size(N) {}

    ///
    /// \brief Construct const span from const std::array
    ///
    /// \tparam U Element type
    /// \tparam N Array size
    /// \param a Reference to const std::array
    ///
    template <typename U, size_type N,
              typename = typename std::enable_if<
                  std::is_same<const U, value_type>::value &&
                  std::is_const<T>::value>::type>
    constexpr span(const std::array<U, N> &a) noexcept
        : m_data(a.data()), m_size(N) {}

    ///
    /// \brief Construct span from container with data() and size()
    ///
    /// \tparam Container Type with data() and size() members
    /// \param c Reference to container
    ///
    template <
        typename Container,
        typename = typename std::enable_if<
            std::is_pointer<
                decltype(std::declval<Container &>().data())>::value &&
            std::is_convertible<decltype(std::declval<Container &>().size()),
                                size_type>::value>::type>
    explicit constexpr span(Container &c) noexcept
        : m_data(c.data()), m_size(static_cast<size_type>(c.size())) {}

    ///
    /// \brief Construct const span from const container
    ///
    /// \tparam Container Type with data() and size() members
    /// \param c Reference to const container
    ///
    template <typename Container,
              typename = typename std::enable_if<
                  std::is_pointer<decltype(std::declval<const Container &>()
                                               .data())>::value &&
                  std::is_convertible<
                      decltype(std::declval<const Container &>().size()),
                      size_type>::value &&
                  std::is_const<T>::value>::type>
    explicit constexpr span(const Container &c) noexcept
        : m_data(c.data()), m_size(static_cast<size_type>(c.size())) {}

    ///
    /// \brief Implicit conversion from span<U> to span<const U>
    ///
    /// \tparam U Element type
    /// \param other Source span
    ///
    template <typename U, typename = typename std::enable_if<
                              std::is_same<const U, T>::value>::type>
    constexpr span(const span<U> &other) noexcept
        : m_data(other.data()), m_size(other.size()) {}

    ///
    /// \brief Get pointer to the underlying data
    ///
    /// \return Pointer to the first element
    ///
    constexpr pointer data() const noexcept { return m_data; }

    ///
    /// \brief Get number of elements in the span
    ///
    /// \return Number of elements
    ///
    constexpr size_type size() const noexcept { return m_size; }

    ///
    /// \brief Check if span is empty
    ///
    /// \return true if size is 0, false otherwise
    ///
    constexpr bool empty() const noexcept { return m_size == 0; }

    ///
    /// \brief Get size in bytes
    ///
    /// \return Total size of all elements in bytes
    ///
    constexpr size_type size_bytes() const noexcept {
        return m_size * sizeof(T);
    }

    ///
    /// \brief Access element at index
    ///
    /// \param index Zero-based element index
    /// \return Reference to element at index
    ///
    constexpr reference operator[](size_type index) const noexcept {
        return m_data[index];
    }

    ///
    /// \brief Access first element
    ///
    /// \return Reference to first element
    ///
    constexpr reference front() const noexcept {
        assert(m_size > 0);
        return m_data[0];
    }

    ///
    /// \brief Access last element
    ///
    /// \return Reference to last element
    ///
    constexpr reference back() const noexcept {
        assert(m_size > 0);
        return m_data[m_size - 1];
    }

    ///
    /// \brief Get iterator to first element
    ///
    /// \return Iterator to the beginning
    ///
    constexpr iterator begin() const noexcept { return m_data; }

    ///
    /// \brief Get iterator to one past last element
    ///
    /// \return Iterator to the end
    ///
    constexpr iterator end() const noexcept { return m_data + m_size; }

    ///
    /// \brief Get const iterator to first element
    ///
    /// \return Const iterator to the beginning
    ///
    constexpr const_iterator cbegin() const noexcept { return m_data; }

    ///
    /// \brief Get const iterator to one past last element
    ///
    /// \return Const iterator to the end
    ///
    constexpr const_iterator cend() const noexcept { return m_data + m_size; }

    ///
    /// \brief Get reverse iterator to last element
    ///
    /// \return Reverse iterator to the beginning (last element)
    ///
    constexpr reverse_iterator rbegin() const noexcept {
        return reverse_iterator(end());
    }

    ///
    /// \brief Get reverse iterator to before first element
    ///
    /// \return Reverse iterator to the end (before first element)
    ///
    constexpr reverse_iterator rend() const noexcept {
        return reverse_iterator(begin());
    }

    ///
    /// \brief Get const reverse iterator to last element
    ///
    /// \return Const reverse iterator to the beginning (last element)
    ///
    constexpr const_reverse_iterator crbegin() const noexcept {
        return const_reverse_iterator(cend());
    }

    ///
    /// \brief Get const reverse iterator to before first element
    ///
    /// \return Const reverse iterator to the end (before first element)
    ///
    constexpr const_reverse_iterator crend() const noexcept {
        return const_reverse_iterator(cbegin());
    }

    ///
    /// \brief Get subspan of first N elements
    ///
    /// \param count Number of elements to include from the start
    /// \return Span containing first count elements
    ///
    constexpr span first(size_type count) const noexcept {
        return span(m_data, count);
    }

    ///
    /// \brief Get subspan of last N elements
    ///
    /// \param count Number of elements to include from the end
    /// \return Span containing last count elements
    ///
    constexpr span last(size_type count) const noexcept {
        return span(m_data + (m_size - count), count);
    }

    ///
    /// \brief Get subspan starting at offset
    ///
    /// \param off Starting offset
    /// \param count Number of elements (default: all remaining)
    /// \return Span containing elements from offset
    ///
    constexpr span
    subspan(size_type off,
            size_type count = static_cast<size_type>(-1)) const noexcept {
        const size_type clamped =
            (count == static_cast<size_type>(-1)) ? (m_size - off) : count;
        return span(m_data + off, clamped);
    }

private:
    pointer m_data;   ///< Pointer to the first element
    size_type m_size; ///< Number of elements in the span
};

///
/// \brief Create span from pointer and size
///
/// \tparam T Element type
/// \param data Pointer to the first element
/// \param size Number of elements
/// \return Span over the specified range
///
template <typename T>
constexpr span<T> make_span(T *data, size_t size) noexcept {
    return span<T>(data, size);
}

///
/// \brief Create span from std::array
///
/// \tparam T Element type
/// \param data std::array of T, count Size
/// \return Span over the specified range
///
template <typename T, size_t Size>
constexpr span<T> make_span(const std::array<T, Size> &data) noexcept {
    return span<T>(data);
}

///
/// \brief Create const span from std::array
///
/// \tparam T Element type
/// \param data std::array of T, count Size
/// \return Const span over the specified range
///
template <typename T, size_t Size>
constexpr span<const T>
make_cspan(const std::array<const T, Size> &data) noexcept {
    return span<const T>(data);
}

///
/// \brief Create const span from pointer and size
///
/// \tparam T Element type
/// \param data Pointer to the first element
/// \param size Number of elements
/// \return Const span over the specified range
///
template <typename T>
constexpr span<const T> make_cspan(const T *data, size_t size) noexcept {
    return span<const T>(data, size);
}

} // namespace sentinel

#endif // SENTINEL_SPAN_HPP
