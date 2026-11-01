#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
if [ "$#" -gt 0 ]; then
    exec python3 tools/ipa_install.py "$@"
fi
exec python3 tools/ipa-drop.py
