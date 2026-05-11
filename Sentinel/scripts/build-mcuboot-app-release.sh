#!/bin/sh

##
## USAGE:
## Invoke while in the Sentinel directory.
##
## % pwd
## /path/to/sentinel-firmware/Sentinel
##
## # Build MCUBoot application release hex file
## % ./scripts/build-mcuboot-app-release.sh nocopy
##
## # Use the "copy" argument to copy the final hex to current directory
## % ./scripts/build-mcuboot-app-release.sh copy
##

function validate_argument() {
    if [ "$#" -ne 1 ]; then
        echo "usage: $0 [copy | nocopy]"
        exit 1
    fi 
}

function try_copy_hex_file() {
    local copy_argument=$1
    local source_hex_path_string=$2
    local destination_hex_string=$3

    ## Copy to the current directory so you can access it easily. Flash it to CM0 with ModusToolbox Programmer.
    if [ $copy_argument != "copy" ]; then
        return
    fi

    cp "$source_hex_path_string" "$destination_hex_string"
}

function main() {
    local command_line_argument=$1

    local username_string=$(whoami)

    local toolchain_path_string="/Users/$username_string/Applications/mtb-gcc-arm-eabi/11.3.1/gcc"
    local gcc_path_string="$toolchain_path_string"
    local cc_string="$gcc_path_string/bin/arm-none-eabi-gcc"

    local mcuboot_cypress_path_string="third-party/mcuboot/boot/cypress"
    local app_name_string="MCUBootApp"
    local platform_string="PSOC_063_1M"
    local flash_map_path_string="../../../../psoc63_1m_cm0_int_swap_single.json"
    local buildcfg_string="Release"
    local destination_hex_string="$app_name_string"_"$buildcfg_string.hex"

    local source_hex_path_string="$mcuboot_cypress_path_string/$app_name_string/out/$platform_string/$buildcfg_string/$app_name_string.hex"

    validate_argument $command_line_argument

    make -C "$mcuboot_cypress_path_string" clean app TOOLCHAIN_PATH="$toolchain_path_string" GCC_PATH="$gcc_path_string" CC="$cc_string" APP_NAME="$app_name_string" PLATFORM="$platform_string" FLASH_MAP="$flash_map_path_string" BUILDCFG="$buildcfg_string"

    try_copy_hex_file $command_line_argument $source_hex_path_string $destination_hex_string
}

main $1
