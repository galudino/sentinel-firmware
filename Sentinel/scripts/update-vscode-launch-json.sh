#!/bin/sh

##
## USAGE:
## Invoke while in the Sentinel directory.
##
## % pwd
## /path/to/sentinel-firmware/Sentinel
##
## # Copy launch.json file into .vscode directory
## % ./scripts/update-vscode-launch-json.sh
##

cp ./vscode-json/launch.json ./.vscode/launch.json
