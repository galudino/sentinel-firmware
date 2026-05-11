#!/bin/sh

## Creates the VSCode workspace and copies JSON files to the `.vscode directory`
make vscode && ./scripts/update-vscode-json.sh
