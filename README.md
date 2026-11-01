# ios-kvm

## The name is inspired by osx-kvm. ARM guests on x86 hosts use QEMU TCG emulation, not KVM.

Run your own iPhone X iOS 16.7.16  firmware in QEMU on Linux. DO NOT USE FOR PIRACY!

`ios-kvm` is the portable software project: users supply their own supported IPSW and keep their firmware, NAND and settings locally. Automatic GPU detection and setup for NVIDIA, AMD and Intel are project requirements; NVIDIA is the only hardware tested so far, and automatic setup remains unfinished. The intended separate private `PeaksiOS` repository is exclusively the owner's NAND backups and personal machine configuration, with no separate renderer implementation. It has not yet been created. Despite the project name, ARM guests on x86 hosts currently use QEMU TCG CPU emulation, not KVM.

The target is a usable iOS interface, SSH, Sileo, GPU acceleration and reliable virtual Ethernet. The VM has reached the home screen and Sileo onboarding, with root SSH and DHCP Ethernet with working DNS/HTTPS. The experimental VirGL bridge renders the desktop on the host GPU, but performance and general app support remain unfinished. See [STATUS.md](STATUS.md) for evidence and remaining work.

## Local use

Use this project with an iPhone X you own. Place your own iPhone X iOS 16.7.16 IPSW in `firmware/`, then run `./setup.sh`; alternatively use `./setup.sh /path/to/your.ipsw`. Setup extracts your firmware, prepares the SDK and support programs, builds the host renderer and iOS GPU bridge, validates the available host renderer, and bundles GPU support into the local images. `IOS_KVM_SETUP_GPU=0 ./setup.sh /path/to/your.ipsw` explicitly builds the baseline without the GPU bridge. Full clean-machine installation remains unverified. Apple firmware and generated images are never bundled in the public repository.

`./run.sh` starts QEMU's native GTK display with OpenGL scaling and zoom-to-fit, four guest CPUs, 6 GiB mapped guest RAM and a 1 GiB translation cache, without a session time limit. The default screen is 750×1624 (2× density); `IOS_KVM_SCALE=3 ./run.sh` selects 1125×2436. Both preserve the phone's logical dimensions. Set `QEMU_SMP`, `QEMU_RAM_GB`, `QEMU_TB_MB` or `VM_DISPLAY` to override the other settings. ARM-on-x86 uses TCG; the CPU is emulated. Current development host: Linux, Ryzen 7 7435HS, 16 GiB RAM, RTX 4060.

The system image is now a read-only virtio disk, with a separate writable HFS data disk. It is read on demand instead of reserving about 2.4 GiB of wired guest RAM. The current UEFI memory layout exposes about 3.1 GiB to XNU from the 6 GiB QEMU allocation. Disk boot requires the locally built kernel with multiple virtio disks and the generated small boot image; rebuild with `bash tools/build-kernel.sh` and `bash tools/build.sh` after updating.

OpenGL in the viewer accelerates host-side display scaling, independently of the experimental guest GPU bridge. The host mouse pointer stays visible, and hovering does not grab it. For hosts without working OpenGL, use `VM_DISPLAY=gtk,zoom-to-fit=on,show-cursor=on ./run.sh`. Diagnostic VNC is opt-in with `QEMU_VNC=1 ./run.sh`; this selects the software GTK viewer because QEMU cannot combine VNC with this OpenGL context.

`bash tools/ssh.sh` connects to root SSH through `127.0.0.1:2222` using a dedicated local key. Password authentication depends on your local SSH configuration and account database. When enabled, diagnostic VNC listens at `127.0.0.1:5901`. The writable data disk retains files and packages across VM restarts; a local first-boot snapshot is imported only once when present. Installed tweak loading and repeated Sileo GUI installations still need verification.

## Tested host

Only tested on Fedora Linux with a Ryzen 7 7435HS, 16 GiB RAM and RTX 4060. AMD and Intel hardware validation remains pending.

Diagnostic VNC has no password authentication and binds only to the local loopback address.

## Graphics development

GPU-enabled boot images now use a managed launch session: `./run.sh` probes real EGL renderers, validates textured shader rendering, blending, BGRA readback with row padding, the host VirGL capability handshake and actual VirGL vertex/fragment shader draws with alpha blending, selects hardware first, and starts/stops the renderer and relay with QEMU. Session logs and the selected profile are kept under `.local/sessions/`. `IOS_KVM_GPU_MODE=software ./run.sh` explicitly tests the software fallback; `VTEST_RENDERNODE=/dev/dri/renderD128 ./run.sh` selects a particular available device. Multiple concurrent managed VMs are not implemented yet. Inspect the profile with `python3 tools/gpu_host.py`. Building the host probe requires `pkg-config` and the EGL/OpenGL development headers and libraries.

A local iOS test process has rendered a verified triangle through Mesa/VirGL on the RTX 4060, reporting OpenGL ES 3.1. The EAGL bridge has reached the home screen through this GPU. An experimental pinned-RAM path removes pixel payloads from the virtual Ethernet link: a 750×1624 repeated-colour test measured median readback of 22 ms versus 125 ms for the previous LZ4 network path, excluding four warm-up frames; all pixel checks passed. This measures readback, not whole-app FPS. Interface stalls, startup cost, general app coverage, and Metal support remain unfinished. AMD and Intel GPUs have not been tested on this host.

Shared-buffer development requires the custom kernel (`bash tools/build-kernel.sh`). `python3 tools/setup-gpu.py` prepares checksum-pinned Mesa 25.0.7 and commit-pinned virglrenderer sources, applies this project's transport/readback changes, builds both sides and the EAGL bridge, then validates the host renderer without starting a VM. Existing source trees are retained; an unexpected virglrenderer revision is rejected. `--prepare-only` skips compilation; `--check-deps` performs no downloads or builds. Use `BUILD_JOBS=2` to bound build concurrency. After a successful GPU build, bundle it with `IOS_KVM_BUNDLE_GPU=1 IOS_KVM_GPU_BOOT=1 bash tools/build.sh`.

GPU build dependencies on Fedora include `meson ninja-build pkgconf-pkg-config libglvnd-devel libepoxy-devel mesa-libgbm-devel python3-mako python3-pyyaml python3-packaging`, plus clang/LLVM and the base setup dependencies. Kernel build dependencies include flex, bison, autoconf/automake, ncurses development headers and libuuid development headers. Install the appropriate working GPU driver for your system; setup does not replace host drivers.

For manual development with an image that does not enable the GPU at boot, start a VM with `QEMU_SHARED_RAM=1 ./run.sh`, then run `python3 tools/start-gpu-server.py` and `python3 tools/virgl-relay.py --compressed` in separate terminals. Shared RAM uses a private anonymous `memfd`, preloaded from the pristine boot image. This avoids continuously writing guest memory to the SSD; the earlier regular-file mapping caused severe writeback and stalls. `QEMU_SHARED_RAM=file` retains the disk-backed diagnostic implementation. `python3 tools/test-shared-memory.py` verifies byte-for-byte host/guest sharing. Build/deploy the bridge with `python3 tools/build-gpu-support.py` and `python3 tools/deploy-gpu.py`, then `python3 tools/switch-gpu.py`; `--disable` restores that session's saved injection configuration. These manual steps remain experimental. The host server accepts the upstream `VTEST_RENDERNODE` environment variable for selecting a render device; driver compatibility and fallback still need verification.

The host renderer reuses its mapping of sealed anonymous guest RAM instead of mapping/unmapping it on every readback. Each transfer still validates the file identity, ownership, permissions, size, seals and destination ranges; regular-file RAM keeps transient mappings. An isolated host transfer test at750×1624 with fresh pixels and two separated RAM segments measured baseline medians3.80/3.87ms versus retained-mapping2.42/2.57ms in alternating runs on the RTX4060. These are host transfer timings, not iOS desktop FPS. Run the no-VM checks with `cc -O2 -Wall -Wextra -Werror tools/test-gpu-ram-map.c -o .local/test-gpu-ram-map && .local/test-gpu-ram-map --bench` and `python3 tools/test-host-readback.py --server .local/virgl-build/vtest/virgl_test_server` (`--mode software` for software rendering).

Backdrop snapshots copy only the enabled scissor rectangle, keeping the original texture coordinates and restoring GL state. Empty or fully clipped draws need no copy. The production shaders and snapshot functions pass 72 offscreen cases per renderer on NVIDIA and llvmpipe, including fresh backgrounds, partial/clipped/empty rectangles and unchanged pixels outside them. `python3 tools/test-backdrop.py --host --mode hardware --benchmark` runs these checks and a copy-only benchmark without a VM; `--mode software` selects software rendering. Guest desktop timing remains pending.

IOSurface change detection uses XXH64 instead of the original serial word hash, and includes trailing bytes that the old loop missed. Run `python3 tools/test-surface-fingerprint.py` for correctness, reference-library comparison when installed, and timing. `--arm` also runs a freestanding ARM calculation through QEMU user mode; it starts no VM. In one alternating generic-ARM build test, a 4.9 MB fingerprint took about0.59ms versus1.16–1.17ms previously. This measures only hashing; full iOS CPU/UI performance remains unverified.

Opacity-first IOSurface uploads convert pixels with unaligned-safe word rotations/byte swaps. `python3 tools/test-surface-pixels.py --arm` checks both channel orders, alignment, untouched guards and flipped rows, then compares the old/new conversion in standalone ARM user mode without starting a VM. The 750×1624 conversion step measured about4.82–4.88ms previously versus4.22–4.26ms with the new helper; this is not a whole-frame or app benchmark. Texture storage reuse remains an experiment: native results were mixed, so normal texture allocation behavior is unchanged.

`python3 tools/test-host-readback.py --server .local/virgl-build/vtest/virgl_test_server --draw` tests actual VirGL vertex/fragment shaders, alternating opaque/alpha-blended full-screen triangles and shared-RAM output at750×1624, without a VM. Every output pixel, padding guards, invalid destination rejection and recovery are checked; use `--mode software` to test the software renderer. GPU selection runs a small two-frame version before accepting each renderer. Its command encoder is built from the pinned upstream test encoder, cached by source/build inputs and host architecture; source attribution/license are retained. NVIDIA and llvmpipe pass; AMD/Intel physical testing and general iOS app coverage remain pending.

`python3 tools/bench-tcg-builds.py /path/to/candidate/qemu-aarch64 --output .local/cpu-comparison.json` compares user-mode emulator builds without booting a VM. It checks arithmetic, indirect-call and memory-workload results against a native reference, then runs alternating baseline/candidate phases. `--cpu N` pins only benchmark children to an allowed host CPU. System-emulator binaries are rejected. Results do not establish full-system iOS speed; the normal launcher continues to use the installed system QEMU.

Add `--targets 4096` to exercise a larger indirect-call working set. The optional [QEMU jump-cache experiment](patches/README.md) includes the pinned source patch, build recipe and invalidation checks. It is not enabled by default; the live iOS reverse comparison did not reproduce the early apparent speedup.

`QEMU_BINARY=/absolute/path/to/qemu-system-aarch64 ./run.sh` selects a custom emulator for both capability checks and the VM. To keep a local selection, write its absolute path on one line in `.local/qemu-binary`; remove that file to use system QEMU again. The environment variable overrides the local selection. The selected path is resolved before the launcher changes directories and is inherited by the managed session.

For actual guest CPU comparisons, `python3 tools/build-guest-tcg-bench.py` builds an iOS benchmark and native checksum reference. With a managed VM running, `python3 tools/bench-live-tcg.py --label candidate --output .local/ios-cpu-candidate.json` deploys the benchmark into temporary guest storage and records timings, actual QEMU binary/hash, session age and host load. It checks all guest results against the native reference and excludes the first two of eight samples. Allow the same startup settling time for each build; startup widget work can otherwise dominate the measurements. These CPU workloads do not measure UI FPS.

`kern.ios_kvm_timer_floor_us` is an experimental CPU timer knob in the custom kernel. Its default is zero, preserving normal deadlines. No timing change is enabled by default, and no performance gain is claimed for it until measured.

`QEMU_KERNEL=/absolute/path/to/candidate ./run.sh` selects an experimental kernel for one boot without replacing the working `vendor/hakios/hakios-kernel`. A missing explicit kernel is an error. The candidate must support the same boot image, disk layout and GPU interfaces; this option does not rebuild them.

Apple firmware, extracted system files and generated device identity data are local inputs and are not distributed in this repository.

## Credits

- OOSEX GANG and contributors: [HakiOS](https://github.com/oosexgang/hakios-xnu-kernel), imported under `vendor/hakios` from revision `a3384e031eb1e605cf37adaefc19991f04f6274b`. Its kernel port, drivers, loader, userland support and image tools provide the current starting components. Original copyright and license files are retained; their terms apply to their respective components.
- dipa-studio and contributors: [ios-sweet](https://github.com/dipa-studio/ios-sweet), used for the first kernel boot experiment in the separate `../ios16-qemu` workspace.
- Apple: open-source XNU and related components, under their respective original licenses.
- QEMU and its contributors: the virtual machine runtime.
- Yann Collet: xxHash, BSD 2-Clause license retained in [vendor/xxhash/xxhash.h](vendor/xxhash/xxhash.h); imported from the pinned Mesa source with a standalone compiler-macro adaptation.
- Mesa and VirGL contributors: graphics implementation and host renderer; sources are fetched locally for development.
- LZ4 contributors: lossless GPU transport compression, imported under `vendor/lz4` from v1.10.0 (`ebb370ca83af193212df4dcbadcc5d87bc0de2f0`); its BSD license is retained.

## Planned device manager

After the CPU/GPU and core VM work is stable, build a GUI to manage virtual devices and guide VM creation using each user's own IPSW, GPU detection, memory selection and persistent storage. Publish the application source in `ios-kvm` on GitHub. The owner's NAND and personal configuration remain separate in the intended private `PeaksiOS` repository.

Pending consecutive touch movements for the same finger now coalesce in the kernel queue. Presses, releases and changes between fingers keep their order. This prevents a busy HID consumer from replaying a long trail of stale pointer positions; it does not make ARM emulation or GPU rendering faster. The bounded-queue overflow policy is unchanged. Run the queue checks with:

```sh
cc -O2 -Wall -Wextra -Werror -I vendor/hakios/xnu/files/bsd \
  -I vendor/hakios/drivers/input tools/test-touch-queue.c -o .local/test-touch-queue
.local/test-touch-queue
```

For frame/input diagnostics, boot with `CMDLINE='touch_lat=1 cpuload=250' ./run.sh`. After a gesture, `python3 tools/capture-kernel-trace.py --output .local/loader-trace.log` briefly pauses the VM, reads its loader trace ring and resumes it. Pass `--kernel` when testing an alternate kernel. Combine that trace with the session's `vm.log` using `tools/analyze-frame-gaps.py --active-touches-only --output .local/frame-gaps.json LOG...` to retain long stalls and report event queue delay during actual finger-down intervals. The upstream `vendor/hakios/tools/frames.py` remains useful for pipeline stage estimates, but its movement summary excludes gaps of100ms or more. Verify which screen the test opened before interpreting an app-specific result.

Shared readback request batching is an opt-in experiment in the guest Mesa library: `IOS_KVM_SHARED_BATCH=1` combines three transport writes into one; unset or `0` retains the original writes. It preserves the vtest command bytes, memory pin ownership and host acknowledgement. Native wire-layout checks cover small, typical and maximum segment counts plus invalid lengths/counts. No iOS performance gain is established yet, and the bundled boot library is unchanged until runtime validation.
