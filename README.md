# ios-kvm

## The name is inspired by osx-kvm. ARM guests on x86 hosts use QEMU TCG emulation, not KVM however KVM will be used for ARM hosts.

Run your own iPhone X iOS 16.7.16  firmware in QEMU on Linux. DO NOT USE FOR PIRACY!

`ios-kvm` is the portable software project: users supply their own supported IPSW and keep their firmware, NAND and settings locally. Automatic GPU detection and setup for NVIDIA, AMD and Intel are project requirements; NVIDIA is the only hardware tested so far, and automatic setup remains unfinished. The intended separate private `PeaksiOS` repository is exclusively the owner's NAND backups and personal machine configuration, with no separate renderer implementation. It has not yet been created. Despite the project name, ARM guests on x86 hosts currently use QEMU TCG CPU emulation, not KVM.

The target is a usable iOS interface, SSH, Sileo, GPU acceleration and reliable virtual Ethernet. The VM has reached the home screen and Sileo onboarding, with root SSH and DHCP Ethernet with working DNS/HTTPS. The experimental VirGL bridge renders the desktop on the host GPU, but performance and general app support remain unfinished. See [STATUS.md](STATUS.md) for evidence and remaining work.

## Local use

Use this project with an iPhone X you own. Place your own iPhone X iOS 16.7.16 IPSW in `firmware/`, then run `./setup.sh`; alternatively use `./setup.sh /path/to/your.ipsw`. Setup extracts your firmware, prepares the SDK and support programs, builds the host renderer and iOS GPU bridge, validates the available host renderer, and bundles GPU support into the local images. `IOS_KVM_SETUP_GPU=0 ./setup.sh /path/to/your.ipsw` explicitly builds the baseline without the GPU bridge. Full clean-machine installation remains unverified. Apple firmware and generated images are never bundled in the public repository.

`./run.sh` starts QEMU's native GTK display with OpenGL scaling and zoom-to-fit, four guest CPUs, 6 GiB mapped guest RAM and a 1 GiB translation cache, without a session time limit. The default screen is 750×1624 (2× density); `IOS_KVM_SCALE=3 ./run.sh` selects 1125×2436. Both preserve the phone's logical dimensions. Set `QEMU_SMP`, `QEMU_RAM_GB`, `QEMU_TB_MB` or `VM_DISPLAY` to override the other settings. ARM-on-x86 uses TCG; the CPU is emulated. Current development host: Linux, Ryzen 7 7435HS, 16 GiB RAM, RTX 4060.

The system image is now a read-only virtio disk, with a separate writable HFS data disk. It is read on demand instead of reserving about 2.4 GiB of wired guest RAM. The current UEFI memory layout exposes about 3.1 GiB to XNU from the 6 GiB QEMU allocation. Disk boot requires the locally built kernel with multiple virtio disks and the generated small boot image; rebuild with `bash tools/build-kernel.sh` and `bash tools/build.sh` after updating.

OpenGL in the viewer accelerates host-side display scaling, independently of the experimental guest GPU bridge. The host mouse pointer stays visible, and hovering does not grab it. For hosts without working OpenGL, use `VM_DISPLAY=gtk,zoom-to-fit=on,show-cursor=on ./run.sh`. Diagnostic VNC is opt-in with `QEMU_VNC=1 ./run.sh`; this selects the software GTK viewer because QEMU cannot combine VNC with this OpenGL context.

`bash tools/ssh.sh` connects to root SSH through `127.0.0.1:2222` using a dedicated local key. Password authentication depends on your local SSH configuration and account database. When enabled, diagnostic VNC listens at `127.0.0.1:5901`. The writable data disk retains files and packages across VM restarts; a local first-boot snapshot is imported only once when present. Installed tweak loading and repeated Sileo GUI installations still need verification.

To restart the iOS interface without rebooting the VM, run `bash tools/respring.sh`. This uses the guest's `sbreload` over SSH, with a SpringBoard restart as a fallback.

## Updating safely

Stop the VM, then run:

```bash
./update.sh --check
./update.sh
```

The updater fetches `Peaks2000/ios-kvm` (`master`) and rebases your local commits onto it. It saves a Git bundle, staged/unstaged patches and a receipt in a private `.local/updates/` directory before changing the checkout. Tracked local settings are temporarily stashed, restored with their staged state, and the recovery stash is retained. Untracked files are left in place; incoming files that collide with them are rejected. Nothing is pushed to GitHub by the updater.

An active VM or unfinished Git operation blocks the update. A rebase conflict triggers an abort and restoration of local edits; if restoring those edits conflicts, resolve the reported files before running or building. The updater prints the recovery directory; `receipt.json` records the original commit and any saved stash. Keep that directory until you have verified the update. Do not use `git reset --hard` to resolve an update failure. Clones from the old rewritten commit history with no common ancestor are refused and need a separate migration.

This updates **project source**, not iOS firmware. Your NAND/data disk, installed apps, tweaks, firmware, SSH keys and generated runtime images stay in place. It does not run `setup.sh`, format a device, rebuild runtime images, or restart the VM. Back up your stopped device storage separately before activating runtime changes; the Git bundle does **not** contain ignored device files. For the current disk/GPU runtime, rebuild while the VM is stopped:

```bash
bash tools/build-kernel.sh
python3 tools/setup-gpu.py
IOS_KVM_BUNDLE_GPU=1 IOS_KVM_GPU_BOOT=1 bash tools/build.sh
```

Only restart after all builds succeed. Read the release instructions before rebuilding across a storage-format or firmware change; automatic data-format migration and runtime rollback are not implemented. Forks can select `--source <repository>` and `--branch <branch>`; only use source repositories you trust.

## Install an IPA

Run `./install-ipa.sh` to open **ios-kvm · Install IPA**, then drag one `.ipa` from your file manager onto that window, or click **Choose IPA…**. The running VM must have working SSH and TrollStore Lite installed. The window shows upload/install progress and errors; no VM restart is needed.

For terminal use: `./install-ipa.sh /path/to/your-app.ipa`. Installation uses the guest's existing [TrollStore Lite](https://github.com/opa334/TrollStore) helper, and temporary guest uploads are removed afterward. IPA files and their contents stay out of the repository. The window requires Python 3, PyGObject and GTK 3 (Fedora: `python3-gobject gtk3`).

## Tested host

Only tested on Fedora Linux with a Ryzen 7 7435HS, 16 GiB RAM and RTX 4060. AMD and Intel hardware validation remains pending.
People have told me it works on some intel macs with no modifications so that is amazing :D

Diagnostic VNC has no password authentication and binds only to the local loopback address.

## Graphics development

GPU-enabled boot images now use a managed launch session: `./run.sh` probes real EGL renderers, validates textured shader rendering, blending, BGRA readback with row padding, the host VirGL capability handshake and actual VirGL vertex/fragment shader draws with alpha blending, selects hardware first, and starts/stops the renderer and relay with QEMU. Session logs and the selected profile are kept under `.local/sessions/`. `IOS_KVM_GPU_MODE=software ./run.sh` explicitly tests the software fallback; `VTEST_RENDERNODE=/dev/dri/renderD128 ./run.sh` selects a particular available device. Multiple concurrent managed VMs are not implemented yet. Inspect the profile with `python3 tools/gpu_host.py`. Building the host probe requires `pkg-config` and the EGL/OpenGL development headers and libraries.

A local iOS test process has rendered a verified triangle through Mesa/VirGL on the RTX 4060, reporting OpenGL ES 3.1. The EAGL bridge has reached the home screen through this GPU. An experimental pinned-RAM path removes pixel payloads from the virtual Ethernet link: a 750×1624 repeated-colour test measured median readback of 22 ms versus 125 ms for the previous LZ4 network path, excluding four warm-up frames; all pixel checks passed. This measures readback, not whole-app FPS. Interface stalls, startup cost, general app coverage, and Metal support remain unfinished. AMD and Intel GPUs have not been tested on this host.

Rebuilt Mesa negotiates pinned-memory uploads with the host on each connection and enables them automatically when advertised. Older project hosts retain ordinary uploads, even with `IOS_KVM_SHARED_UPLOAD=1`; `IOS_KVM_SHARED_UPLOAD=0` in the guest renderer disables sharing for comparisons. Rebuild both GPU components and the bundled boot image to retain this behavior across restarts. Eligible uploads bypass the virtual network for tightly packed, uncompressed four-byte transfers of at least 256 KiB; smaller and unsupported layouts retain the ordinary path. Source pages are unpinned after the synchronous host acknowledgement. A 4.9 MB changing-texture ABBA test measured 425–559 ms normally versus 23–26 ms with sharing on NVIDIA, and 401–548 ms versus 18 ms on llvmpipe. These are upload timings, not whole-app FPS or proof of AMD/Intel hardware compatibility.

Rebuilt Mesa sends each vtest command header and body in one framed socket write. This preserves command order and submission timing, works through the existing relay/older project host servers, and is enabled by default. `IOS_KVM_COMMAND_PACKETS=0` in the guest renderer restores separate writes; unset, `auto` or `1` enables batching. A same-process NVIDIA Settings comparison over two ABBA cycles measured 16.51 versus 17.27 display updates/s (about 4.6% improvement), with long stalls still present. This is one scrolling workload, not general app FPS or physical AMD/Intel validation. Rebuild GPU components and bundle the boot image to retain it across restarts.

For same-process development comparisons, `IOS_KVM_COMMAND_PACKETS=marker` maps `/var/tmp/ios-kvm-command-packets-control` once at process startup. The file must be a regular, private four-byte file owned by the renderer's effective UID (the bundled backboardd runs as mobile/501); little-endian value `1` enables batching and `0` disables it. Update those bytes in place without truncating or replacing the mapped file. Missing/invalid control files retain separate writes. Normal automatic mode needs no control file.

For development, `IOS_KVM_HOST_SERVER=/absolute/path/to/virgl_test_server ./run.sh` selects a separately built host server. `IOS_KVM_VTEST_PORT` in a guest test process can select a private loopback test relay; the default stays 6712. `tools/build-gpu-support.py` builds the upload benchmark, and `tools/deploy-gpu.py` includes it when present. The guest `gpu-bench upload 750 1624`, `upload-subrect`, and `upload-mip` modes check freshly uploaded pixel patterns, row layout/borders and mip storage; compare `IOS_KVM_SHARED_UPLOAD=0` and `1` on the same matching renderer. Existing clear/readback benchmarks remain available.

Rebuilt Mesa also negotiates synchronous readback completion per connection, omitting the preliminary resource-busy request when the updated host advertises that guarantee. Flush and synchronous readback remain required. Older hosts retain the preliminary wait. `IOS_KVM_READBACK_SKIP_WAIT=0` in the **guest renderer process** forces the wait; `1` explicitly skips it, including on legacy hosts; unset or `auto` uses negotiation. For controlled comparisons without resprings, `IOS_KVM_READBACK_SKIP_WAIT=marker` checks for `/var/tmp/ios-kvm-readback-skip-wait` on each readback; creating/removing that guest file toggles only this experiment. A same-process NVIDIA Settings comparison measured 13.68 versus 15.49 display updates/s (about 13% improvement). This is one scrolling workload, with remaining stalls; AMD/Intel validation and broader application coverage are pending.

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

On an ARM64 Linux host, `QEMU_ACCEL=kvm ./run.sh` explicitly requests hardware CPU virtualization. The launcher checks the host architecture, read/write access to the `/dev/kvm` character device and whether the selected QEMU includes KVM. It uses `-cpu host -accel kvm`, without TCG cache options. An unavailable requested accelerator is an error, with no silent fallback. Default execution remains TCG with `cortex-a76`; x86 hosts cannot use KVM for this ARM guest. This launch path needs prepared images and a suitable host toolchain: clean ARM-host setup and actual iOS-on-KVM boot compatibility are **not yet validated**. KVM does not provide the guest GPU bridge. See [QEMU's ARM CPU documentation](https://www.qemu.org/docs/master/system/arm/cpu-features).

For actual guest CPU comparisons, `python3 tools/build-guest-tcg-bench.py` builds an iOS benchmark and native checksum reference. With a managed VM running, `python3 tools/bench-live-tcg.py --label candidate --output .local/ios-cpu-candidate.json` deploys the benchmark into temporary guest storage and records timings, actual QEMU binary/hash, session age and host load. It checks all guest results against the native reference and excludes the first two of eight samples. Allow the same startup settling time for each build; startup widget work can otherwise dominate the measurements. These CPU workloads do not measure UI FPS.

An optional [second-chance jump-cache experiment](patches/qemu-victim-cache.md) checks a displaced entry before QEMU's global translation lookup. It is a separate manual QEMU patch, with explicit code-modification and remapping checks. Calculation results are mixed, and a matched iOS Settings comparison showed no UI gain. It is not selected by the launcher.

`kern.ios_kvm_timer_floor_us` is an experimental CPU timer knob in the custom kernel. Its default is zero, preserving normal deadlines. No timing change is enabled by default, and no performance gain is claimed for it until measured.

`QEMU_KERNEL=/absolute/path/to/candidate ./run.sh` selects an experimental kernel for one boot without replacing the working `vendor/hakios/hakios-kernel`. A missing explicit kernel is an error. The candidate must support the same boot image, disk layout and GPU interfaces; this option does not rebuild them.

The kernel also has a developer-only completion API for future asynchronous surface readbacks. It requires both `gpu_shared=1` and `gpu_async=1` boot arguments and is disabled for ordinary launches. It validates the existing owning-task pin against kernel-backed IOSurface pixels, retains pending backing independently of the submitting app, and bounds retention to 128 surfaces / 64 MiB of pixels. CPU locks wait for completion; an unfinished host frame preserves the previous panel image. The GPU transport still uses synchronous readback: this API alone provides no frame-rate improvement. With a deliberately selected candidate kernel running, `python3 tools/test-surface-fences.py --output .local/surface-fences/results.json` builds and runs the completion/lifetime checks. This diagnostic currently requires an x86 Linux host, the local iOS 16.5 SDK/toolchain and private shared RAM; it checks normal/error/timeout completion, task exit and both retention limits. Pending requests from a failed diagnostic remain retained until acknowledged or the VM shuts down.

Apple firmware, extracted system files and generated device identity data are local inputs and are not distributed in this repository.

## Credits

- OOSEX GANG and contributors: [HakiOS](https://github.com/oosexgang/hakios-xnu-kernel), imported under `vendor/hakios` from revision `a3384e031eb1e605cf37adaefc19991f04f6274b`. Its kernel port, drivers, loader, userland support and image tools provide the current starting components. Original copyright and license files are retained; their terms apply to their respective components.
- dipa-studio and contributors: [ios-sweet](https://github.com/dipa-studio/ios-sweet), used for the first kernel boot experiment in the separate `../ios16-qemu` workspace.
- Apple: open-source XNU and related components, under their respective original licenses.
- QEMU and its contributors: the virtual machine runtime.
- Yann Collet: xxHash, BSD 2-Clause license retained in [vendor/xxhash/xxhash.h](vendor/xxhash/xxhash.h); imported from the pinned Mesa source with a standalone compiler-macro adaptation.
- Mesa and VirGL contributors: graphics implementation and host renderer; sources are fetched locally for development.
- LZ4 contributors: lossless GPU transport compression, imported under `vendor/lz4` from v1.10.0 (`ebb370ca83af193212df4dcbadcc5d87bc0de2f0`); its BSD license is retained.

## GUI device manager coming soon

