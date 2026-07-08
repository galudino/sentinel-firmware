///
/// \file    sentinel_gatt_system.hpp
/// \brief   Accessor layer for the custom \c System GATT service (#6 / #45)
///
/// \details Thin \c sentinel::gatt::system seam over the generated \c System
///          service value arrays: Serial Number (R/W device-identity key),
///          Firmware Version (R, 5-byte packed), Request Bootloader Mode (W),
///          and Platform ID (R, \ref sentinel::platform_id). Like the rest of
///          the \c sentinel::gatt::<svc> layer these are \c inline + \c noexcept
///          (the values are \c extern GATT-DB globals, not constant
///          expressions) and keep the generated \c app_system_* / \c HDLC_SYSTEM_*
///          symbols out of the call sites.
///
/// \author  galudino
/// \date    2026-07-08
/// \version 1.0
///

#ifndef SENTINEL_GATT_SYSTEM_HPP
#define SENTINEL_GATT_SYSTEM_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include "cycfg_gatt_db.h"

#include "wiced_bt_gatt.h"
}
#pragma GCC diagnostic pop

#include "sentinel_ble_gatt.hpp"
#include "sentinel_firmware_version.hpp"
#include "sentinel_platform_id.hpp"
#include "sentinel_utilities.hpp"

#include <cstdint>

namespace sentinel::gatt::system {

/// \brief Current device Serial Number (little-endian device-identity key).
inline uint32_t serial_number() noexcept {
    return static_cast<uint32_t>(app_system_serial_number[0]) |
           (static_cast<uint32_t>(app_system_serial_number[1]) << 8) |
           (static_cast<uint32_t>(app_system_serial_number[2]) << 16) |
           (static_cast<uint32_t>(app_system_serial_number[3]) << 24);
}

/// \brief Set the Serial Number characteristic (little-endian).
inline void set_serial_number(uint32_t serial) noexcept {
    uint8_t le[4] = {static_cast<uint8_t>(serial & 0xFFu),
                     static_cast<uint8_t>((serial >> 8) & 0xFFu),
                     static_cast<uint8_t>((serial >> 16) & 0xFFu),
                     static_cast<uint8_t>((serial >> 24) & 0xFFu)};
    ble_gatt_db_set_value(HDLC_SYSTEM_SERIAL_NUMBER_VALUE, le,
                          static_cast<uint16_t>(sizeof(le)));
}

/// \brief Set the Firmware Version characteristic (5-byte packed, little-endian:
///        major, minor, patch, build[2]).
inline void set_firmware_version(const sentinel::firmware_version &v) noexcept {
    // build() truncates to 8 bits in the current firmware_version API; array()
    // carries the full 16-bit build, matching the 5-byte wire struct (#6).
    const auto build = v.array()[3];
    uint8_t buf[5] = {v.major(), v.minor(), v.patch(),
                      static_cast<uint8_t>(build & 0xFFu),
                      static_cast<uint8_t>((build >> 8) & 0xFFu)};
    ble_gatt_db_set_value(HDLC_SYSTEM_FIRMWARE_VERSION_VALUE, buf,
                          static_cast<uint16_t>(sizeof(buf)));
}

/// \brief Set the Platform ID characteristic.
inline void set_platform_id(sentinel::platform_id p) noexcept {
    app_system_platform_id[0] = sentinel::to_underlying(p);
}

/// \brief Current Platform ID characteristic value.
inline sentinel::platform_id platform_id() noexcept {
    return static_cast<sentinel::platform_id>(app_system_platform_id[0]);
}

} // namespace sentinel::gatt::system

#endif /* SENTINEL_GATT_SYSTEM_HPP */
