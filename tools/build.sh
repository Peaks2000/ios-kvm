#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../vendor/hakios"
xnuloader/build.sh
python3 ../../tools/build-support.py
if [ -f ../../.local/session/var-firstboot.tar ]; then
    export RC=rc.vm
    export EXTRA_SHARE="$PWD/../../.local/session"
fi
if [ "${IOS_KVM_BUNDLE_GPU:-0}" = 1 ]; then
    bundle="$PWD/../../.local/gpu-bundle"
    mkdir -p "$bundle"
    for library in gl_test libOSMesa.8.dylib libhakios.gl.dylib libios-kvm-gpu.dylib; do
        cp "../../.local/support/$library" "$bundle/"
    done
    export EXTRA_SHARE="${EXTRA_SHARE:-} $bundle"
fi
SUPPORT_DIR="$PWD/../../.local/support" PREBUILT=1 REST=1 WIFID=0 RC_JOBS=rc.jobs.vm userland/build.sh
if [ "${IOS_KVM_GPU_BOOT:-0}" = 1 ]; then
    [ "${IOS_KVM_BUNDLE_GPU:-0}" = 1 ] || { echo 'GPU boot needs IOS_KVM_BUNDLE_GPU=1.' >&2; exit 1; }
    python3 - <<'PY'
from pathlib import Path
path = Path('userland/root/etc/inject.conf')
lines = []
for line in path.read_text().splitlines():
    parts = line.split()
    if not parts:
        continue
    names = [name for name in parts[1:] if name != 'backboardd']
    if names:
        lines.append(' '.join([parts[0]] + names))
lines.append('/redwood/share/gpu-bundle/libios-kvm-gpu.dylib backboardd')
path.write_text('\n'.join(lines) + '\n')
PY
    (cd userland; python3 ../tools/apfs/mkapfs.py -z --plain fw -o root.apfs redwood=root)
fi
python3 ../../tools/build-disk-boot.py
