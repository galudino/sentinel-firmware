///
/// \file    sentinel_platform_id.hpp
/// \brief   Machine-stable platform / vendor identity (GATT + cloud wire
///          contract) — fw#45
///
/// \details Defines the \ref sentinel::platform_id discriminator exposed over
///          the \c System GATT service (fw#6) and the \ref sentinel::vendor_id
///          it derives, plus \ref sentinel::vendor_of. This is the single source
///          of truth shared by the GATT layer (the Platform ID characteristic
///          and the Device Information Service Manufacturer Name) and, later, the
///          cloud manifest \c target key. The iOS client (\c sentinel-client #9)
///          mirrors these values 1:1.
///
///          \b Wire \b contract. Both enums are transmitted over BLE and reused
///          as cloud-manifest target keys, so treat the values like protobuf
///          field numbers — explicit, append-only, never reorder, never reuse a
///          retired value. Vendor is a \e function of platform (one source of
///          truth), not its own characteristic; it exists only to populate the
///          DIS Manufacturer Name string.
///
///          Canonical platform tokens — one set across firmware \c platform:
///          labels <-> this \ref sentinel::platform_id <-> cloud manifest
///          \c target: \c cyble-416045 / \c rpi5 / \c nrf5340.
///
/// \author  galudino
/// \date    2026-07-08
/// \version 1.0 - Initial platform / vendor identity
///

#ifndef SENTINEL_PLATFORM_ID_HPP
#define SENTINEL_PLATFORM_ID_HPP

#include <cstdint>

namespace sentinel {

///
/// \brief Machine-stable hardware-platform discriminator.
///
/// \details The client and cloud branch on \b this, never on the DIS display
///          strings (Model Number, Hardware Revision). Append-only wire
///          contract — see the file header.
///
enum class platform_id : uint8_t {
    unknown      = 0x00, ///< Unrecognized / not-yet-resolved platform.
    cyble_416045 = 0x01, ///< Infineon CYBLE-416045-EVAL.
    rpi5         = 0x02, ///< Raspberry Pi 5.
    nrf5340      = 0x03, ///< Nordic Semiconductor nRF5340.
};

///
/// \brief Hardware vendor. A \e function of \ref sentinel::platform_id (see
///        \ref sentinel::vendor_of), used only to populate the DIS
///        Manufacturer Name.
///
/// \details Append-only wire contract — see the file header.
///
enum class vendor_id : uint8_t {
    unknown              = 0x00, ///< Unrecognized / not-yet-resolved vendor.
    infineon             = 0x01, ///< Infineon Technologies.
    raspberry_pi_ltd     = 0x02, ///< Raspberry Pi Ltd.
    nordic_semiconductor = 0x03, ///< Nordic Semiconductor.
};

///
/// \brief The vendor that makes a given platform.
///
/// \param p Platform to resolve.
/// \return The owning \ref sentinel::vendor_id, or
///         \ref sentinel::vendor_id::unknown for an unrecognized platform.
///
constexpr vendor_id vendor_of(platform_id p) noexcept {
    switch (p) {
    case platform_id::cyble_416045:
        return vendor_id::infineon;
    case platform_id::rpi5:
        return vendor_id::raspberry_pi_ltd;
    case platform_id::nrf5340:
        return vendor_id::nordic_semiconductor;
    default:
        return vendor_id::unknown;
    }
}

///
/// \brief The platform this firmware image was built for.
///
/// \details Phase I ships on the Infineon CYBLE-416045-EVAL. When a second
///          target is added, select this from a build-time \c COMPONENT_/target
///          macro rather than hard-coding it at every call site.
///
/// \return The \ref sentinel::platform_id this image was built for.
///
constexpr platform_id current_platform_id() noexcept {
#if defined(COMPONENT_APP_CYBLE_416045_EVAL) ||                                \
    defined(TARGET_APP_CYBLE_416045_EVAL)
    return platform_id::cyble_416045;
#else
    return platform_id::cyble_416045;
#endif
}

} // namespace sentinel

#endif /* SENTINEL_PLATFORM_ID_HPP */
