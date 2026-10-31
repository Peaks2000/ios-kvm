#!/bin/bash
# Build the VM kernel, including ios-kvm's opt-in pinned-buffer transfer interface.
set -euo pipefail
cd "$(dirname "$0")/.."
root=$PWD
export THEOS="$root/.local/theos"
export JOBS=${BUILD_JOBS:-4}
for tool in curl tar xz make cc clang clang++ flex bison m4 perl python3 autoreconf automake; do
    command -v "$tool" >/dev/null || { echo "Missing kernel build tool: $tool" >&2; exit 1; }
done
[ -f .local/sdks/iPhoneOS16.5.sdk/usr/include/stdio.h ] || {
    echo 'Run setup.sh with your IPSW first to prepare the SDK.' >&2; exit 1;
}
if [ ! -x "$THEOS/toolchain/linux/iphone/bin/ld" ]; then
    [ "$(uname -m)" = x86_64 ] || { echo 'This toolchain download currently supports x86_64 Linux.' >&2; exit 1; }
    archive="$root/.local/iOSToolchain-x86_64.tar.xz"
    curl -fL https://github.com/L1ghtmann/llvm-project/releases/download/test-210562a/iOSToolchain-x86_64.tar.xz -o "$archive"
    printf '%s  %s\n' a72a7a577e2fbe2838b6b5e9c72034fa7d114af96f0e1d4b016f18730ce4056e "$archive" | sha256sum -c -
    mkdir -p "$THEOS/toolchain"
    tar -xJf "$archive" -C "$THEOS/toolchain"
fi
mkdir -p "$THEOS/sdks"
ln -sfn ../../sdks/iPhoneOS16.5.sdk "$THEOS/sdks/iPhoneOS16.5.sdk"
cd vendor/hakios
bash xnu-deps/setup.sh --stage1
[ -d xnu-src/xnu-8796.121.2 ] || bash xnu/apply.sh
python3 tools/hakios-config.py defconfig qemu
python3 tools/hakios-config.py kernel --tree xnu-src/xnu-8796.121.2
# apply.sh installs this on a fresh tree; update existing trees after our API changes.
for file in iokit/Kernel/MiniIOSurface.cpp osfmk/arm64/generic_arm64_support.c; do
    if ! cmp -s "xnu/files/$file" "xnu-src/xnu-8796.121.2/$file"; then
        cp -p "xnu/files/$file" "xnu-src/xnu-8796.121.2/$file"
    fi
done
python3 "$root/tools/apply-cpu-tuning.py"
bash xnu-deps/setup.sh --stage2
bash tools/make-kernel.sh -j"$JOBS"
