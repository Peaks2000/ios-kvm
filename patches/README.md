# QEMU jump-cache experiment

This optional patch increases QEMU's per-vCPU translated-block jump cache from
4,096 to 65,536 entries (approximately 64 KiB to 1 MiB per vCPU). It changes
neither the generated ARM instructions nor the existing invalidation logic.
Larger caches also increase invalidation work. This is an experiment, and the
normal launcher still uses the installed QEMU.

The patch applies to upstream QEMU 10.2.2, whose source archive SHA256 is
`784b296ff29c1417aa72323abcb2d2ea9ab9771724f577dcd785c3b04f21e176`.
QEMU is credited in the main README; its original copyright notices and GPL
license remain in its source. Obtain the release from
<https://download.qemu.org/qemu-10.2.2.tar.xz> and verify the checksum before
extracting it into `.local/qemu-10.2.2`.

From the project root, apply once:

```sh
patch -d .local/qemu-10.2.2 -p1 --forward < patches/qemu-10.2.2-jump-cache.patch
python3 tools/test-tcg-cache-layout.py
```

Example separate system build, with GTK, OpenGL, networking and PipeWire:

```sh
mkdir -p .local/qemu-native-system-build
cd .local/qemu-native-system-build
../qemu-10.2.2/configure --target-list=aarch64-softmmu --enable-lto \
  --disable-debug-info --disable-docs --disable-guest-agent --disable-tools \
  --disable-plugins --disable-werror --enable-gtk --enable-opengl \
  --enable-slirp --enable-vnc --enable-pipewire --extra-cflags=-march=native \
  --extra-ldflags=-flto=2 -Doptimization=3 -Db_lto_threads=2
ninja -j1 qemu-system-aarch64
```

Install the development dependencies required by configure first. The build
does not install or replace system QEMU. `-march=native` targets the build host;
rebuild on a different machine rather than distributing that binary as a generic
release. The build host used GCC and supports these flags.

For isolated user-mode checks, use a separate build directory with
`--target-list=aarch64-linux-user` and omit the GTK/OpenGL/slirp/VNC/PipeWire
options. Keep an unpatched build for comparison:

```sh
python3 tools/test-tcg-invalidation.py /path/to/patched/qemu-aarch64 \
  --baseline /path/to/unpatched/qemu-aarch64
python3 tools/bench-tcg-builds.py /path/to/patched/qemu-aarch64 \
  --baseline /path/to/unpatched/qemu-aarch64 --targets 4096 \
  --output .local/tcg-wide-comparison.json
```

The layout tests passed for user/system hash functions at both cache sizes and
four guest page sizes. User-mode modified-code, protection-change and address
reuse tests passed. A 4,096-target user-mode workload measured about 151 ms
unpatched versus 106 ms patched in one alternating comparison; the small
four-target workload regressed in the same run. These results do not prove a
full-system or iOS speedup. SMP invalidation, real UI performance and boot
behavior still need system-mode validation before adopting this patch.

`python3 tools/test-tcg-system.py /path/to/qemu-system-aarch64` boots a disposable
128 MiB, single-vCPU machine with no disks or Apple firmware. It checks modified
code before and after enabling the ARM MMU, code reached through a virtual
alias, and 512 break-before-make remappings of that alias between physical
pages. It exits through semihosting and does not open a viewer, network or
monitor. This exercises system-mode invalidation; it does not validate SMP or
replace a real iOS boot and performance comparison.

Both the installed QEMU and the native/LTO/cache16 system build pass these
diskless checks. The custom build also reports the required virt machine,
ramfb, virtio block/network/sound devices, GTK, PipeWire and TCG, with all shared
libraries resolved. Audio playback and the full iOS UI have not been tested
with this binary.

For a deliberate full iOS comparison when host memory permits, select this
build only for that launch, from the project root:

```sh
PATH="$PWD/.local/qemu-native-system-build:$PATH" ./run.sh
```

Ordinary `./run.sh` continues to select the installed system QEMU. Keep the
kernel, disks, GPU renderer and guest CPU/RAM settings constant when comparing.
