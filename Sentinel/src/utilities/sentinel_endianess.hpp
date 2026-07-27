///
/// \file    sentinel_endianess.hpp
/// \brief   Byte order utilities for multi-byte data operations
///
/// \details This header provides utilities for handling byte order (endianness)
///          in multi-byte data operations. These functions are essential for
///          embedded systems that communicate with sensors and peripherals
///          that may use different byte ordering conventions.
///
///          Key features:
///          - Endianness enumeration for specifying byte order
///          - Template functions for reading integral values from byte buffers
///          - Template functions for converting integral values to byte arrays
///          - Support for both big-endian and little-endian byte orders
///          - Compile-time type safety with static assertions
///
///          These utilities are particularly useful for:
///          - Reading multi-byte sensor registers
///          - Writing multi-byte configuration values
///          - Protocol implementations requiring specific byte order
///          - Cross-platform data serialization
///
/// \date    2024-2025
/// \version 1.0 - Endianness utilities
///

#ifndef SENTINEL_ENDIANESS_HPP
#define SENTINEL_ENDIANESS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace sentinel {

///
/// \brief Enumeration for byte order specification
///
/// \details Specifies the endianness (byte order) for multi-byte data
///          operations. Used in template-based register operations
///          to handle different sensor byte ordering requirements.
///
///          - Big-endian: Most significant byte stored at lowest address
///            (network byte order, used by Motorola, SPARC)
///          - Little-endian: Least significant byte stored at lowest address
///            (used by x86, ARM in little-endian mode)
///
enum class endianess {
    big,   ///< Big-endian byte order (most significant byte first)
    little ///< Little-endian byte order (least significant byte first)
};

///
/// \brief Read an integral value from a byte buffer (pointer overload)
///
/// \details Converts a byte buffer into an integral value using the specified
///          endianness. Supports both signed and unsigned integral types.
///          The function uses compile-time type checking to ensure only
///          integral types are used.
///
/// \tparam T Integral type to read (must satisfy std::is_integral)
/// \param buffer Pointer to byte buffer (must have at least sizeof(T) bytes)
/// \param order Byte order: big-endian (default) or little-endian
/// \return The integral value reconstructed from the byte buffer
///
/// \note Big-endian: most significant byte first (buffer[0] is MSB)
///       Little-endian: least significant byte first (buffer[0] is LSB)
///
/// \warning Ensure buffer has at least sizeof(T) bytes to avoid undefined
///          behavior. No bounds checking is performed for performance.
///
template <typename T>
constexpr T read_integral(const uint8_t *buffer,
                          endianess order = endianess::big);

///
/// \brief Read an integral value from a byte buffer (array overload)
///
/// \details Converts a byte array into an integral value using the specified
///          endianness. This function is useful for reading multi-byte sensor
///          registers where byte order matters.
///
/// \tparam T Integral type to read (e.g., uint16_t, uint32_t, int16_t)
/// \param buffer Array of bytes to read from (size must match sizeof(T))
/// \param order Byte order: big-endian (default) or little-endian
/// \return The integral value reconstructed from the byte buffer
///
/// \note This overload forwards to the pointer-based version for
///       implementation reuse.
///
template <typename T>
constexpr T read_integral(const std::array<uint8_t, sizeof(T)> &buffer,
                          endianess order = endianess::big);

///
/// \brief Convert an integral value to a byte array
///
/// \details Converts an integral value into a byte array using the specified
///          endianness. This function is useful for writing multi-byte values
///          to sensor registers where byte order matters. Supports both signed
///          and unsigned integral types.
///
/// \tparam T Integral type to convert (must satisfy std::is_integral)
/// \param value The integral value to convert to bytes
/// \param order Byte order: big-endian (default) or little-endian
/// \return Array of bytes representing the value in specified byte order
///
/// \note Big-endian: most significant byte first (bytes[0] is MSB)
///       Little-endian: least significant byte first (bytes[0] is LSB)
///
/// \par Example
/// \code
/// // Convert a 16-bit value to big-endian bytes
/// uint16_t value = 0x1234;
/// auto bytes = to_bytes(value, endianess::big);
/// // Result: bytes[0] = 0x12, bytes[1] = 0x34
///
/// // Convert to little-endian bytes
/// auto bytes_le = to_bytes(value, endianess::little);
/// // Result: bytes_le[0] = 0x34, bytes_le[1] = 0x12
/// \endcode
///
template <typename T>
constexpr std::array<uint8_t, sizeof(T)>
to_bytes(T value, endianess order = endianess::big) noexcept;

} // namespace sentinel

template <typename T>
constexpr T sentinel::read_integral(const uint8_t *buffer, endianess order) {
    static_assert(std::is_integral_v<T>, "T must be an integral type");

    using unsigned_t = std::make_unsigned_t<T>;
    unsigned_t value = 0;

    switch (order) {
    case endianess::little:
        // Little-endian: LSB first
        for (auto i = size_t{}; i < sizeof(T); i++) {
            value |= static_cast<unsigned_t>(buffer[i]) << (8 * i);
        }
        return static_cast<T>(value);

    case endianess::big:
    default:
        // Big-endian: MSB first
        for (auto i = size_t{}; i < sizeof(T); i++) {
            value = (value << 8) | static_cast<unsigned_t>(buffer[i]);
        }
        return static_cast<T>(value);
    }
}

template <typename T>
constexpr T
sentinel::read_integral(const std::array<uint8_t, sizeof(T)> &buffer,
                        endianess order) {
    return read_integral<T>(buffer.data(), order);
}

template <typename T>
constexpr std::array<uint8_t, sizeof(T)>
sentinel::to_bytes(T value, endianess order) noexcept {
    static_assert(std::is_integral_v<T>, "T must be an integral type");

    using unsigned_t = std::make_unsigned_t<T>;
    std::array<std::uint8_t, sizeof(T)> bytes{};

    unsigned_t u = static_cast<unsigned_t>(value);

    switch (order) {
    case endianess::little:
        // Little-endian: LSB first
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            bytes[i] = static_cast<std::uint8_t>(u & 0xFFu);
            u >>= 8;
        }
        return bytes;

    case endianess::big:
    default:
        // Big-endian: MSB first
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            // Fill from the end for big-endian
            bytes[sizeof(T) - 1 - i] = static_cast<std::uint8_t>(u & 0xFFu);
            u >>= 8;
        }
        return bytes;
    }
}

#endif // SENTINEL_ENDIANESS_HPP
