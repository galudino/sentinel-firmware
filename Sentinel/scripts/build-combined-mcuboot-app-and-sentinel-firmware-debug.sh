#!/bin/sh

##
## USAGE:
## Invoke while in the Sentinel directory.
##
## % pwd
## /path/to/sentinel-firmware/Sentinel
##
## # Build combined MCUBoot and sentinel-firmware debug hex file
## % ./scripts/build-combined-mcuboot-app-and-sentinel-firmware-debug.sh nocopy
##
## # Use the "copy" argument to copy the final combined hex to current directory
## % ./scripts/build-combined-mcuboot-app-and-sentinel-firmware-debug.sh copy
##

function main() {
    ## Build the MCUBoot application
    ./scripts/build-mcuboot-app-debug.sh nocopy

    ## Build the main application (sentinel-firmware)
    ./scripts/build-sentinel-firmware-debug.sh nocopy

    ## Combine the MCUBoot and main application hex files
    ./scripts/combine-mcuboot-app-and-sentinel-firmware-debug.sh
}

main
