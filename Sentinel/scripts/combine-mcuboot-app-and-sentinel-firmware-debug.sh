#!/bin/sh

##
## USAGE:
## Invoke while in the Sentinel directory.
##
## % pwd
## /path/to/sentinel-firmware/Sentinel
##
## # Combine MCUBootApp and sentinel-firmware release hex file
## % ./scripts/combine-mcuboot-app-and-sentinel-firmware-debug.sh
##

function main() {
    local config="Debug"
    local timestamp_string="$(date +"%Y.%m.%d_%H.%M.%S")"

    local mcuboot_app_name_string="MCUBootApp"
    local app_name_string="sentinel-firmware"
    local combined_name_string="$mcuboot_app_name_string"_"$app_name_string"_"$config"
    local combined_name_string_timestamped="$combined_name_string"_"$timestamp_string"

    local srecord_path_string="${CY_TOOLS_PATHS:-/Applications/ModusToolbox/tools_3.8}/srecord/bin/srec_cat"

    local boot_hex_string="./third-party/mcuboot/boot/cypress/"$mcuboot_app_name_string"/out/PSOC_063_1M/$config/$mcuboot_app_name_string.hex"
    local app_hex_string="./build/APP_CYBLE-416045-EVAL/$config/$app_name_string.hex"
    local combined_hex_string="./$combined_name_string_timestamped.hex"

    "$srecord_path_string" "$boot_hex_string" -Intel "$app_hex_string" -Intel -o "$combined_hex_string" -Intel
}

main
