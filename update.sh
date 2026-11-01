#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
exec python3 tools/update-repo.py "$@"
