#!/bin/sh

##
## USAGE:
## Invoke while in the Sentinel directory.
##
## % pwd
## /path/to/sentinel-firmware/Sentinel
##
## # Build combined MCUBoot and sentinel-testbench release hex file
## % ./scripts/build-combined-mcuboot-app-and-sentinel-testbench-release.sh nocopy
##
## # Use the "copy" argument to copy the final combined hex to current directory
## % ./scripts/build-combined-mcuboot-app-and-sentinel-testbench-release.sh copy
##

function main() {
    ## Build the MCUBoot application
    ./scripts/build-mcuboot-app-release.sh nocopy

    ## Build the testbench application (sentinel-testbench)
    ./scripts/build-sentinel-testbench-release.sh nocopy

    ## Combine the MCUBoot and testbench application hex files
    ./scripts/combine-mcuboot-app-and-sentinel-testbench-release.sh
}

main
