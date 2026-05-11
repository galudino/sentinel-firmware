#!/bin/sh

##
## USAGE:
## Invoke while in the Sentinel directory.
##
## % pwd
## /path/to/sentinel-firmware/Sentinel
##
## # Copy tasks.json and launch.json files into .vscode directory
## % ./scripts/update-vscode-json.sh
##

./scripts/update-vscode-tasks-json.sh && ./scripts/update-vscode-launch-json.sh
