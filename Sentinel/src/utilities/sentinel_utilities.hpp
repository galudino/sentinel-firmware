///
/// \file    sentinel_utilities.hpp
/// \brief   Common utilities and constants
///
/// \details This header provides shared utilities, constants, and helper
///          functions.
///
/// \author  galudino
/// \date    2025
/// \version 1.0 - Initial utilities and constants implementation
///

#ifndef SENTINEL_UTILITIES_HPP
#define SENTINEL_UTILITIES_HPP

#include <type_traits>

namespace sentinel {

/// \name Template Utilities
/// \{

///
/// \brief Suppress unused variable warnings
///
/// \details Template function to explicitly mark variables as unused,
///          suppressing compiler warnings while maintaining code clarity.
///          This is particularly useful in embedded development where
///          variables may be conditionally used based on build configuration.
///
/// \tparam Ts Types of the unused values (automatically deduced)
/// \param args The values to mark as unused
///
/// \note Binds each argument by \c const reference and discards it, so it works
///       with non-copyable and expensive-to-copy types — e.g.
///       \c sentinel::unused(sentinel::resource::context()) — unlike a by-value
///       parameter, which would try to (and here, fail to) copy the argument.
///       Accepts any number of arguments. \c constexpr with zero runtime
///       overhead.
///
/// \code{.cpp}
/// void example_function(int used_param, int unused_param) {
///     // Use the first parameter
///     process_data(used_param);
///
///     // Suppress warning for unused parameter
///     sentinel::unused(unused_param);
/// }
/// \endcode
///
template <typename... Ts>
constexpr void unused(const Ts &...args) noexcept {
    (static_cast<void>(args), ...);
}

///
/// \brief Identity function object for template operations
///
/// \details A function object that returns its argument unchanged. This is
///          useful as a default template parameter for transformation functions
///          in template-based I2C operations where no data transformation
///          is needed.
///
/// \note This struct is constexpr and noexcept, providing zero runtime
///       overhead for template instantiations.
///
struct identity_function {
    ///
    /// \brief Function call operator that returns the input unchanged
    ///
    /// \tparam T Type of the input value (automatically deduced)
    /// \param value The value to return unchanged
    /// \return The input value without modification
    ///
    template <typename T>
    constexpr T operator()(T value) const noexcept {
        return value;
    }
};

///
/// \brief Enumeration for byte order specification
///
/// \details Specifies the endianness (byte order) for multi-byte data
///          operations. Used in template-based I2C register operations
///          to handle different sensor byte ordering requirements.
///
enum class endianess {
    big,   ///< Big-endian byte order (most significant byte first)
    little ///< Little-endian byte order (least significant byte first)
};

///
/// \brief Convert enum class to its underlying integral type
///
/// \details Template function that safely converts an enum class value
///          to its underlying integral type. This is useful for register
///          address operations where enum classes provide type safety
///          but integral values are needed for hardware operations.
///
/// \tparam E Enum class type (must be an enum)
/// \param e The enum value to convert
/// \return The underlying integral value of the enum
///
/// \note This function includes a static_assert to ensure it's only
///       used with enum types, providing compile-time type safety.
///
template <typename E>
constexpr std::underlying_type_t<E> to_underlying(E e) noexcept {
    static_assert(std::is_enum<E>::value,
                  "sentinel::to_underlying requires an enum type");
    return static_cast<std::underlying_type_t<E>>(e);
}

///
/// \brief Type trait to check if an enum has a 1-byte underlying type
///
/// \details Compile-time constant that evaluates to true if the given
///          type is an enum with a 1-byte (uint8_t) underlying type.
///          This is used to constrain template functions to work only
///          with register address enums that fit in a single byte.
///
/// \tparam E Type to check (should be an enum class)
///
/// \note This is used in SFINAE (Substitution Failure Is Not An Error)
///       contexts to enable template functions only for appropriate types.
///
template <typename E>
static constexpr bool is_byte_enum_v =
    std::is_enum<E>::value && sizeof(std::underlying_type_t<E>) == 1;

/// \}

} // namespace sentinel

#endif /* SENTINEL_UTILITIES_HPP */
