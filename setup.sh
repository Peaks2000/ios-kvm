#!/bin/bash
# Build local images from the owner's IPSW. No Apple firmware is distributed.
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$PWD
gpu=${IOS_KVM_SETUP_GPU:-1}
case "$gpu" in
    0|1) ;;
    *) echo 'IOS_KVM_SETUP_GPU must be 0 or 1.' >&2; exit 1 ;;
esac
mkdir -p .local firmware
if [ -n "${1:-}" ]; then
    IPSW=$(realpath "$1")
else
    shopt -s nullglob
    files=(firmware/*.ipsw)
    if [ "${#files[@]}" = 1 ]; then
        IPSW=$(realpath "${files[0]}")
    else
        read -r -p 'Path to your iPhone X iOS 16.7.16 IPSW: ' IPSW
        IPSW=$(realpath "$IPSW")
    fi
fi
[ -f "$IPSW" ] || { echo 'IPSW not found.' >&2; exit 1; }
missing=()
for tool in python3 git cc make clang clang++ lld-link llvm-nm qemu-system-aarch64 7z lz4 curl flex bison m4 perl autoreconf automake mkfs.hfsplus; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
if [ "${#missing[@]}" != 0 ]; then
    echo "Missing tools: ${missing[*]}" >&2
    echo 'Fedora dependencies: clang lld llvm qemu-system-aarch64 qemu-ui-gtk edk2-aarch64 7zip lz4 gcc make git curl python3-fonttools flex bison m4 perl autoconf automake ncurses-devel libuuid-devel; also provide mkfs.hfsplus (hfsprogs).' >&2
    exit 1
fi
python3 -c 'import fontTools' || { echo 'Install python3-fonttools.' >&2; exit 1; }
if [ "$gpu" = 1 ]; then
    python3 tools/setup-gpu.py --check-deps || {
        echo 'GPU build dependencies on Fedora: meson ninja-build pkgconf-pkg-config libglvnd-devel libepoxy-devel mesa-libgbm-devel python3-mako python3-pyyaml python3-packaging; LLVM tools are also required.' >&2
        exit 1
    }
fi
python3 tools/configure-firmware.py "$IPSW"
if [ ! -d .local/sdks/iPhoneOS16.5.sdk/usr/include ]; then
    if [ ! -d .local/sdks/.git ]; then
        git clone --depth 1 --filter=blob:none --sparse https://github.com/theos/sdks.git .local/sdks
    fi
    git -C .local/sdks sparse-checkout set iPhoneOS16.5.sdk
fi
if [ ! -x vendor/hakios/xnu-deps/host/bin/lzfse ]; then
    [ -d .local/lzfse ] || git clone --depth 1 https://github.com/lzfse/lzfse.git .local/lzfse
    make -C .local/lzfse -j4
    mkdir -p vendor/hakios/xnu-deps/host/bin
    cp .local/lzfse/build/bin/lzfse vendor/hakios/xnu-deps/host/bin/
fi
(
    cd vendor/hakios
    KEEP=1 sysimage/unpack.sh "$IPSW"
    xnuloader/mkidentity.sh sysimage/DeviceTree.d22ap.im4p
    [ -d jb/root/var/jb/Applications/Sileo.app ] || tools/mk-jb.sh
    mkdir -p ssh
    [ -f ssh/client_ed25519 ] || ssh-keygen -q -t ed25519 -N '' -C ios-kvm-client -f ssh/client_ed25519
    [ -f ssh/ssh_host_ed25519_key ] || ssh-keygen -q -t ed25519 -N '' -C ios-kvm-host -f ssh/ssh_host_ed25519_key
    cp ssh/client_ed25519.pub ssh/authorized_keys
)
bash "$ROOT/tools/build-kernel.sh"
python3 "$ROOT/tools/setup-storage.py"
if [ "$gpu" = 1 ]; then
    python3 "$ROOT/tools/setup-gpu.py"
    IOS_KVM_BUNDLE_GPU=1 IOS_KVM_GPU_BOOT=1 bash "$ROOT/tools/build.sh"
else
    bash "$ROOT/tools/build.sh"
fi
echo 'Local iOS images built. Run ./run.sh to start the VM.'
