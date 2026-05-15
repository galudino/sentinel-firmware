///
/// \file    sentinel_firmware_version.hpp
/// \brief   Compile-time firmware version management
///
/// \details This header provides a constexpr-based firmware version class that
///          builds version strings at compile time for efficient embedded use.
///          The version follows semantic versioning with four components:
///          major.minor.patch.build format.
///
///          Key features:
///          - Compile-time string construction using constexpr
///          - Zero runtime overhead for version string generation
///          - Semantic versioning support with four-part version numbers
///          - Multiple output formats (string, array, individual components)
///          - Embedded-friendly design with fixed buffer sizes
///
///          Version component guidelines:
///          - major: Major overhauls of the codebase
///          - minor: Multiple related feature completions
///          - patch: Newly completed individual features
///          - build: New builds and minor fixes
///
/// \author  galudino
/// \date    2026-05-15
/// \version 1.0 - Initial firmware version management implementation
///

#ifndef SENTINEL_FIRMWARE_VERSION_HPP
#define SENTINEL_FIRMWARE_VERSION_HPP

#include "sentinel_utilities.hpp"

#include <array>
#include <cstdint>

namespace sentinel {

///
/// \brief Compile-time firmware version class
///
/// \details A constexpr-enabled class that constructs version strings at
///          compile time, providing zero-overhead version management for
///          embedded firmware applications. Supports semantic versioning
///          with four components and multiple output formats.
///
class firmware_version {
public:
    ///
    /// \brief Construct firmware version with four-part version number
    ///
    /// \details Constructs a firmware version object at compile time,
    ///          building the version string during compilation for zero
    ///          runtime overhead. The version string follows the format
    ///          "major.minor.patch.build" (e.g., "2.1.3.1801").
    ///
    /// \param major Major version number (0-255). Default is APP_VERSION_MAJOR.
    /// \param minor Minor version number (0-255). Default is APP_VERSION_MINOR.
    /// \param patch Patch version number (0-255). Default is APP_VERSION_PATCH.
    /// \param build Build version number (0-65535). Default is
    /// APP_VERSION_BUILD.
    ///
    /// \note This constructor is constexpr and builds the version string
    ///       at compile time, ensuring no runtime performance impact.
    ///
    constexpr explicit firmware_version(uint8_t major = APP_VERSION_MAJOR,
                                        uint8_t minor = APP_VERSION_MINOR,
                                        uint8_t patch = APP_VERSION_PATCH,
                                        uint16_t build = APP_VERSION_BUILD)
        : m_major(major), m_minor(minor), m_patch(patch), m_build(build),
          m_buf{}, m_padding{} {
        auto pos = 0;
        pos += append_uint8(m_major, m_buf + pos);
        m_buf[pos++] = '.';
        pos += append_uint8(m_minor, m_buf + pos);
        m_buf[pos++] = '.';
        pos += append_uint8(m_patch, m_buf + pos);
        m_buf[pos] = '\0';
        pos += append_uint16(m_build, m_buf + pos);
        m_buf[pos++] = '.';
    }

    /// \brief Get major version component
    /// \return Major version number (0-255)
    constexpr uint8_t major() const { return m_major; }

    /// \brief Get minor version component
    /// \return Minor version number (0-255)
    constexpr uint8_t minor() const { return m_minor; }

    /// \brief Get patch version component
    /// \return Patch version number (0-255)
    constexpr uint8_t patch() const { return m_patch; }

    /// \brief Get build version component
    /// \return Build version number (0-255)
    constexpr uint8_t build() const { return m_build; }

    ///
    /// \brief Get version as null-terminated C string
    ///
    /// \details Returns a pointer to the compile-time constructed version
    ///          string in "major.minor.patch.build" format. The string is
    ///          guaranteed to be null-terminated and valid for the lifetime
    ///          of the version object.
    ///
    /// \return Pointer to null-terminated version string
    ///
    /// \note The returned string is constructed at compile time and has
    ///       zero runtime overhead.
    ///
    constexpr const char *c_str() const { return m_buf; }

    ///
    /// \brief Get version components as std::array<uint16_t, 4>
    ///
    /// \details Returns the four version components as a std::array for
    ///          convenient access and iteration. The array contains the
    ///          version components in order: [major, minor, patch, build].
    ///
    /// \return std::array<uint16_t, 4> containing version components
    ///
    /// \note This method is constexpr and can be used in compile-time
    ///       contexts for template metaprogramming or constexpr algorithms.
    ///
    constexpr std::array<uint16_t, 4> array() const {
        return std::array<uint16_t, 4>{m_major, m_minor, m_patch, m_build};
    }

private:
    ///
    /// \brief Convert uint8_t to decimal string at compile time
    ///
    /// \details Converts an 8-bit unsigned integer to its decimal string
    ///          representation without leading zeros. This function is
    ///          constexpr and designed for compile-time string construction.
    ///          Handles values from 0-255 efficiently.
    ///
    /// \param value The uint8_t value to convert (0-255)
    /// \param out Pointer to output buffer (must have space for 3+ chars)
    ///
    /// \return Number of characters written to output buffer (1-3)
    ///
    /// \note This function does not null-terminate the output. The caller
    ///       is responsible for proper buffer management and termination.
    ///
    static constexpr int append_uint8(uint8_t value, char *out) {
        auto len = 0;
        if (value >= 100) {
            out[len++] = static_cast<char>('0' + (value / 100));
            value %= 100;
        }
        if (value >= 10 || len > 0) {
            out[len++] = static_cast<char>('0' + (value / 10));
            value %= 10;
        }
        out[len++] = static_cast<char>('0' + value);
        return len;
    }

    ///
    /// \brief Convert uint16_t to decimal string at compile time
    ///
    /// \details Converts an 16-bit unsigned integer to its decimal string
    ///          representation without leading zeros. This function is
    ///          constexpr and designed for compile-time string construction.
    ///          Handles values from 0-65535 efficiently.
    ///
    /// \param value The uint16_t value to convert (0-65535)
    /// \param out Pointer to output buffer (must have space for 5+ chars)
    ///
    /// \return Number of characters written to output buffer (1-5)
    ///
    /// \note This function does not null-terminate the output. The caller
    ///       is responsible for proper buffer management and termination.
    ///
    static constexpr int append_uint16(uint16_t value, char *out) {
        auto len = 0;
        if (value >= 10000) {
            out[len++] = static_cast<char>('0' + (value / 10000));
            value %= 10000;
        }
        if (value >= 1000 || len > 0) {
            out[len++] = static_cast<char>('0' + (value / 1000));
            value %= 1000;
        }
        if (value >= 100 || len > 0) {
            out[len++] = static_cast<char>('0' + (value / 100));
            value %= 100;
        }
        if (value >= 10 || len > 0) {
            out[len++] = static_cast<char>('0' + (value / 10));
            value %= 10;
        }
        out[len++] = static_cast<char>('0' + value);
        return len;
    }

    uint8_t m_major;  ///< Major version component (0-255)
    uint8_t m_minor;  ///< Minor version component (0-255)
    uint8_t m_patch;  ///< Patch version component (0-255)
    uint16_t m_build; ///< Build version component (0-65535)

    /// Internal buffer for version string storage
    /// Size: "255.255.255.65535\0" = 18 characters maximum
    char m_buf[18];

    uint8_t m_padding;
};

/// \name Firmware Version Management
/// \{

///
/// \brief Current firmware version instance
///
/// \details The official firmware version for this build. Version components
///          should be updated according to semantic versioning principles:
///
///          - Increment m_build for new builds and minor fixes
///          - Increment m_patch for newly completed individual features
///          - Increment m_minor after multiple, related feature completions
///          - Increment m_major for major overhauls of the codebase
///
///          Current version: 0.0.0.0 (Major release)
///
/// \note This version is shared across both CM0P and CM4 cores and should
///       be the single source of truth for firmware version information.
///
constexpr auto current_firmware_version = firmware_version();

/// \}

} // namespace sentinel

#endif /* SENTINEL_FIRMWARE_VERSION_HPP */
