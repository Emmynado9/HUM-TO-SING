#!/bin/bash
set -u
repo_root="$(cd "$(dirname "$0")" && pwd)"
if "$repo_root/scripts/build-intel-mac.sh"; then
    printf '\nYour app is in build/macos-intel/packages/HumToSing.app.\n'
    read -r -p 'Press Return to close this Terminal window. ' answer || true
else
    result=$?
    printf '\nNo completed Mac app was produced. Read the error above, fix the prerequisite, and retry.\n'
    read -r -p 'Press Return to close this Terminal window. ' answer || true
    exit "$result"
fi
