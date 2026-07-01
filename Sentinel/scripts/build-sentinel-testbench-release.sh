#!/bin/sh

##
## USAGE:
## Invoke while in the Sentinel directory.
##
## % pwd
## /path/to/sentinel-firmware/Sentinel
##
## # Build Sentinel application debug hex file
## % ./scripts/build-sentinel-testbench-release.sh nocopy
##
## # Use the "copy" argument to copy the final hex to current directory
## % ./scripts/build-sentinel-testbench-release.sh copy
##

function validate_argument() {
    if [ "$#" -ne 1 ]; then
        echo "usage: $0 [copy | nocopy]"
        exit 1
    fi
}

function start_python_env() {
    python3 -m venv ~/.mtb-venv
    source ~/.mtb-venv/bin/activate
    pip install click cryptography intelhex
}

function try_copy_bin_file() {
    local copy_argument=$1
    local source_bin_path_string=$2
    local destination_bin_string=$3

    ## Copy to the current directory so you can access it easily. This .bin file is used for OTA DFU.
    if [ $copy_argument != "copy" ]; then
        return
    fi

    cp "$source_bin_path_string" "$destination_bin_string"
}

function main() {
    local command_line_argument=$1

    local config="Release"
    local app_name_string="sentinel-testbench"
    local testbench_mode=1
    local app_bin_string="./build/TARGET_CYBLE-416045-EVAL/$config/$app_name_string.bin"
    local timestamp_string="$(date +"%Y.%m.%d_%H.%M.%S")"
    local destination_bin_string="$app_name_string""_"$config"_"$timestamp_string".bin"

    validate_argument $command_line_argument

    start_python_env
    make build CONFIG=$config APPNAME=$app_name_string TESTBENCH=$testbench_mode
    deactivate

    try_copy_bin_file $command_line_argument $app_bin_string $destination_bin_string
}

main $1
