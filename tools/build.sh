#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../vendor/hakios"
xnuloader/build.sh
python3 ../../tools/build-support.py
if [ -f ../../.local/session/var-firstboot.tar ]; then
    export RC=rc.vm
    export EXTRA_SHARE="$PWD/../../.local/session"
fi
SUPPORT_DIR="$PWD/../../.local/support" PREBUILT=1 REST=1 WIFID=0 RC_JOBS=rc.jobs.vm userland/build.sh
