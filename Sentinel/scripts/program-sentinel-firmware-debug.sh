#!/bin/sh

##
## USAGE:
## Invoke while in the Sentinel directory.
##
## % pwd
## /path/to/sentinel-firmware/Sentinel
##
## # Program the POC's CM4 core with the debug build of sentinel-firmware.
## % ./scripts/program-sentinel-firmware-debug.sh
##
## IMPORTANT:
## Run this script AFTER you have run ./build-mcuboot-app-debug.sh and flashed MCUBootApp_Debug.hex.
## You can use ModusToolbox Programmer to flash MCUBootApp_Debug.hex.
## mcuboot will automatically be flashed to Core CM0.

## Also note: For Bluetooth to work properly, *** DON'T USE THIS BUILD **.
## Bluetooth will not work as intended in the Debug config. (unless you're in the debugger)
##

function start_python_env() {
    python3 -m venv ~/.mtb-venv
    source ~/.mtb-venv/bin/activate
    pip install click cryptography intelhex
}

function main() {
    local config="Debug"
    local app_name="sentinel-firmware"

    start_python_env
    make program CONFIG=$config APPNAME=$app_name
    deactivate
}

main
