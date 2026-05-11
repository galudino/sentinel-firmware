#!/bin/sh

##
## USAGE:
## Invoke while in the Sentinel directory.
##
## % pwd
## /path/to/sentinel-firmware/Sentinel
##
## # Copy tasks.json file into .vscode directory
## % ./scripts/update-vscode-tasks-json.sh
##

cp ./vscode-json/tasks.json ./.vscode/tasks.json
