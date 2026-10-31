#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
if [ ! -s vendor/hakios/userland/system.apfs ] || [ ! -s vendor/hakios/userland/root.apfs ]; then
    bash setup.sh
fi
exec bash tools/run.sh "${1:-0}"
