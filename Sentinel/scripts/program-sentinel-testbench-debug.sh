#!/bin/sh

##
## USAGE:
## Invoke while in the Sentinel directory.
##
## % pwd
## /path/to/sentinel-firmware/Sentinel
##
## # Program the POC's CM4 core with the debug build of sentinel-testbench.
## % ./scripts/program-sentinel-testbench-debug.sh
##
## IMPORTANT:
## Run this script AFTER you have run ./build-mcuboot-app-debug.sh and flashed MCUBootApp_Debug.hex to the POC.
## You can use ModusToolbox Programmer to flash MCUBootApp_Debug.hex to the POC.
## mcuboot will automatically be flashed to Core CM0.

## Also note: For Bluetooth to work properly, *** USE THIS BUILD **.
## Bluetooth will work as intended in the Release config.
##

function start_python_env() {
    python3 -m venv ~/.mtb-venv
    source ~/.mtb-venv/bin/activate
    pip install click cryptography intelhex
}

function main() {
    local config="Debug"
    local app_name="sentinel-testbench"
    local testbench_mode=1

    start_python_env
    make program CONFIG=$config APPNAME=$app_name TESTBENCH=$testbench_mode
    deactivate
}

main
