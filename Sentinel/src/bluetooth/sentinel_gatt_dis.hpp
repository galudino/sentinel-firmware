///
/// \file    sentinel_gatt_dis.hpp
/// \brief   Device Information Service (0x180A) population (#45)
///
/// \details Populates the standard DIS characteristics at boot. Manufacturer
///          Name is derived from \ref sentinel::vendor_of (single source of
///          truth, #45); Firmware Revision mirrors \ref
///          sentinel::firmware_version; Serial Number mirrors the \c System
///          service Serial Number. Model Number and Hardware Revision are static
///          display strings baked into the GATT database (design.cybt) and need
///          no runtime write. PnP ID carries a documented placeholder Vendor ID.
///
///          Display strings (Model Number, Hardware Revision) are never parsed
///          for logic — logic branches on \ref sentinel::platform_id.
///
/// \author  galudino
/// \date    2026-07-08
/// \version 1.0
///

#ifndef SENTINEL_GATT_DIS_HPP
#define SENTINEL_GATT_DIS_HPP

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

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sentinel::gatt::dis {

/// \brief Human-readable manufacturer name for the DIS, derived from the vendor.
inline const char *manufacturer_name(sentinel::vendor_id v) noexcept {
    switch (v) {
    case sentinel::vendor_id::infineon:             return "Infineon Technologies";
    case sentinel::vendor_id::raspberry_pi_ltd:     return "Raspberry Pi Ltd";
    case sentinel::vendor_id::nordic_semiconductor: return "Nordic Semiconductor";
    default:                                        return "Unknown";
    }
}

/// \brief Write a UTF-8 string to a DIS characteristic (bounded by its max_len).
inline void set_string(uint16_t handle, const char *s, uint16_t max_len) noexcept {
    auto n = static_cast<uint16_t>(std::strlen(s));
    if (n > max_len) {
        n = max_len;
    }
    ble_gatt_db_set_value(
        handle, reinterpret_cast<uint8_t *>(const_cast<char *>(s)), n);
}

/// \brief Mirror the \c System Serial Number into the DIS Serial Number string
///        (uppercase hex, e.g. 0x0000000A -> "0000000A").
inline void set_serial_number(uint32_t serial) noexcept {
    char buf[MAX_LEN_DIS_SERIAL_NUMBER_STRING + 1];
    std::snprintf(buf, sizeof(buf), "%08lX",
                  static_cast<unsigned long>(serial));
    set_string(HDLC_DIS_SERIAL_NUMBER_STRING_VALUE, buf,
               MAX_LEN_DIS_SERIAL_NUMBER_STRING);
}

///
/// \brief Populate the runtime DIS fields (Manufacturer / Firmware Rev / Serial).
///
/// \details Model Number + Hardware Revision are static display strings already
///          in the GATT database; PnP ID is a static placeholder. Call once at
///          boot after the \c System service values are seeded.
///
/// \param p      Platform this image runs on (drives Manufacturer via vendor_of).
/// \param v      Firmware version to mirror into Firmware Revision.
/// \param serial Serial Number to mirror from the System service.
///
inline void populate(sentinel::platform_id p, const sentinel::firmware_version &v,
                     uint32_t serial) noexcept {
    set_string(HDLC_DIS_MANUFACTURER_NAME_STRING_VALUE,
               manufacturer_name(sentinel::vendor_of(p)),
               MAX_LEN_DIS_MANUFACTURER_NAME_STRING);
    set_string(HDLC_DIS_FIRMWARE_REVISION_STRING_VALUE, v.c_str(),
               MAX_LEN_DIS_FIRMWARE_REVISION_STRING);
    set_serial_number(serial);
}

} // namespace sentinel::gatt::dis

#endif /* SENTINEL_GATT_DIS_HPP */
