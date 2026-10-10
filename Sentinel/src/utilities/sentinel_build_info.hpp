///
/// \file    sentinel_build_info.hpp
/// \brief   Compile-time build identity: channel / config / variant (fw#73)
///
/// \details Complements \ref sentinel::firmware_version: the version says
///          \e which build is running, \ref sentinel::build_info says \e what
///          \e kind — an official release vs a release candidate vs a
///          work-in-progress image, a Release vs Debug configuration, and the
///          application vs the testbench variant. Every field is a compile-time
///          constant sourced from build-system defines, exposed read-only over
///          the \c System GATT service (the Firmware Build Info
///          characteristic).
///
///          \b Wire \b contract. The three enums are transmitted over BLE as
///          <tt>[channel, config, variant]</tt> and mirrored 1:1 by the iOS
///          client (\c sentinel-client #42), so treat the values like protobuf
///          field numbers — explicit, append-only, never reorder, never reuse a
///          retired value.
///
///          \b Sources. \c channel comes from the \c SENTINEL_BUILD_CHANNEL
///          Makefile variable (\c wip by default; set to \c rc / \c release at
///          a candidate / release cut); \c config from the existing
///          \c RELEASE_CONFIG / \c DEBUG_CONFIG defines (\c CONFIG=Release|Debug);
///          \c variant from the existing \c SENTINEL_TESTBENCH define.
///
/// \author  galudino
/// \date    2026-10-10
/// \version 1.0 - Initial build identity
///

#ifndef SENTINEL_BUILD_INFO_HPP
#define SENTINEL_BUILD_INFO_HPP

#include <cstdint>

namespace sentinel {

///
/// \brief Release maturity of the running image.
///
/// \details Append-only wire contract — see the file header.
///
enum class build_channel : uint8_t {
    release = 0x00,           ///< Official tagged release.
    release_candidate = 0x01, ///< Hand-built / CI release candidate.
    wip = 0x02,               ///< Work-in-progress development image.
};

///
/// \brief Compiler configuration the image was built with.
///
/// \details Append-only wire contract — see the file header.
///
enum class build_config : uint8_t {
    release = 0x00, ///< \c CONFIG=Release (optimized).
    debug = 0x01,   ///< \c CONFIG=Debug.
};

///
/// \brief Which application the image is.
///
/// \details Append-only wire contract — see the file header.
///
enum class build_variant : uint8_t {
    app = 0x00,       ///< The Sentinel firmware application.
    testbench = 0x01, ///< The serial-driven testbench application.
};

///
/// \brief The three build-identity fields, in wire order.
///
struct build_info {
    build_channel channel; ///< Release maturity.
    build_config config;   ///< Compiler configuration.
    build_variant variant; ///< Application variant.
};

#ifndef SENTINEL_BUILD_CHANNEL
/// \brief Numeric \ref sentinel::build_channel of this image; the Makefile
///        normally defines it (default: \c wip).
#define SENTINEL_BUILD_CHANNEL 2
#endif

static_assert(SENTINEL_BUILD_CHANNEL >= 0 && SENTINEL_BUILD_CHANNEL <= 2,
              "SENTINEL_BUILD_CHANNEL must be 0 (release), 1 (rc) or 2 (wip)");

/// \brief Build identity of the running image, resolved at compile time.
inline constexpr build_info current_build_info{
    static_cast<build_channel>(SENTINEL_BUILD_CHANNEL),
#if defined(DEBUG_CONFIG)
    build_config::debug,
#else
    build_config::release,
#endif
#if defined(SENTINEL_TESTBENCH)
    build_variant::testbench,
#else
    build_variant::app,
#endif
};

} // namespace sentinel

#endif /* SENTINEL_BUILD_INFO_HPP */
