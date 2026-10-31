#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../vendor/hakios"
mkdir -p logs
export QEMU_NET=1
export QEMU_RAM_GB=${QEMU_RAM_GB:-9}
export QEMU_SMP=${QEMU_SMP:-4}
export CMDLINE="cpus=${QEMU_SMP} ${CMDLINE:-}"
export QEMU_SSH_PORT=${QEMU_SSH_PORT:-2222}
export QEMU_EFI=${QEMU_EFI:-/usr/share/AAVMF/AAVMF_CODE.fd}
export QEMU_VARS=${QEMU_VARS:-/usr/share/AAVMF/AAVMF_VARS.fd}
exec qemu/run-darwin.sh "${1:-0}" -display "${VM_DISPLAY:-gtk,zoom-to-fit=on}" -vnc 127.0.0.1:1
