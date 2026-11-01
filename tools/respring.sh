#!/bin/bash
# Restart the guest's iOS interface without rebooting its VM.
set -euo pipefail
cd "$(dirname "$0")"
if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    echo 'Usage: bash tools/respring.sh'
    echo 'Respring the running ios-kvm guest over SSH (no VM reboot).'
    exit 0
fi
if (( $# )); then
    echo 'Usage: bash tools/respring.sh' >&2
    exit 2
fi
echo 'Requesting an iOS respring…'
exec bash ./ssh.sh -o BatchMode=yes -o ConnectTimeout=8 \
    -o ServerAliveInterval=5 -o ServerAliveCountMax=3 '
export PATH=/var/jb/usr/bin:/var/jb/usr/sbin:/var/jb/bin:/var/jb/sbin:/usr/bin:/usr/sbin:/bin:/sbin
if [ -x /var/jb/usr/bin/sbreload ]; then
    exec /var/jb/usr/bin/sbreload
elif command -v killall >/dev/null 2>&1; then
    exec killall -TERM SpringBoard
else
    echo "No supported respring utility is installed in this guest." >&2
    exit 1
fi
'
