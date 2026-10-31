#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../vendor/hakios"
mkdir -p logs
export QEMU_NET=1
if [ -z "${AUDIO:-}" ]; then
    audio_drivers=$(qemu-system-aarch64 -audiodev help)
    if printf '%s\n' "$audio_drivers" | rg -q '^pipewire$' &&
       [ -S "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/pipewire-0" ]; then
        export AUDIO=pipewire
    elif printf '%s\n' "$audio_drivers" | rg -q '^pa$' &&
         [ -S "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/pulse/native" ]; then
        export AUDIO=pa
    else
        export AUDIO=none
    fi
fi
export QEMU_RAM_GB=${QEMU_RAM_GB:-9}
export QEMU_SMP=${QEMU_SMP:-4}
export QEMU_TB_MB=${QEMU_TB_MB:-1024}
scale=${IOS_KVM_SCALE:-2}
case "$scale" in
    2|3) ;;
    *) echo 'IOS_KVM_SCALE must be 2 (750x1624) or 3 (1125x2436).' >&2; exit 1 ;;
esac
export CMDLINE="cpus=${QEMU_SMP} fbscale=$scale ${CMDLINE:-}"
export QEMU_SSH_PORT=${QEMU_SSH_PORT:-2222}
export QEMU_EFI=${QEMU_EFI:-/usr/share/AAVMF/AAVMF_CODE.fd}
export QEMU_VARS=${QEMU_VARS:-/usr/share/AAVMF/AAVMF_VARS.fd}
if [ -z "${QEMU_DATA_IMAGE:-}" ] && [ -f ../../.local/data.hfs.img ]; then
    export QEMU_DATA_IMAGE="$PWD/../../.local/data.hfs.img"
fi
display=${VM_DISPLAY:-gtk,gl=on,zoom-to-fit=on,show-cursor=on,grab-on-hover=off}
extra=()
if [ "${QEMU_VNC:-0}" = 1 ]; then
    display=${VM_DISPLAY:-gtk,zoom-to-fit=on,show-cursor=on,grab-on-hover=off}
    extra=(-vnc 127.0.0.1:1)
fi
exec qemu/run-darwin.sh "${1:-0}" -display "$display" "${extra[@]}"
