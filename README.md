# ios-kvm

An independent project to run the user's iPhone X iOS 16.7.16 (20H392) firmware in QEMU on Linux.

`ios-kvm` is the portable software project: users supply their own supported IPSW and keep their firmware, NAND and settings locally. Automatic GPU detection and setup for NVIDIA, AMD and Intel are project requirements; NVIDIA is the only hardware tested so far, and automatic setup remains unfinished. The intended separate private `PeaksiOS` repository is exclusively the owner's NAND backups and personal machine configuration, with no separate renderer implementation. It has not yet been created. Despite the project name, ARM guests on x86 hosts currently use QEMU TCG CPU emulation, not KVM.

The target is a usable iOS interface, SSH, Sileo, GPU acceleration and reliable virtual Ethernet. The VM has reached the home screen and Sileo onboarding, with root SSH and DHCP Ethernet with working DNS/HTTPS. The experimental VirGL bridge renders the desktop on the host GPU, but performance and general app support remain unfinished. See [STATUS.md](STATUS.md) for evidence and remaining work.

## Local use

Place your own iPhone X iOS 16.7.16 IPSW in `firmware/`, then run `./setup.sh`; alternatively use `./setup.sh /path/to/your.ipsw`. The setup script extracts your files, prepares the SDK and support programs, and builds the local system images. Apple firmware and generated images are never bundled in the public repository.

`./run.sh` starts QEMU's native GTK display with OpenGL scaling and zoom-to-fit, four guest CPUs, 6 GiB mapped guest RAM and a 1 GiB translation cache, without a session time limit. The default screen is 750×1624 (2× density); `IOS_KVM_SCALE=3 ./run.sh` selects 1125×2436. Both preserve the phone's logical dimensions. Set `QEMU_SMP`, `QEMU_RAM_GB`, `QEMU_TB_MB` or `VM_DISPLAY` to override the other settings. ARM-on-x86 uses TCG; the CPU is emulated. Current development host: Linux, Ryzen 7 7435HS, 16 GiB RAM, RTX 4060.

The system image is now a read-only virtio disk, with a separate writable HFS data disk. It is read on demand instead of reserving about 2.4 GiB of wired guest RAM. The current UEFI memory layout exposes about 3.1 GiB to XNU from the 6 GiB QEMU allocation. Disk boot requires the locally built kernel with multiple virtio disks and the generated small boot image; rebuild with `bash tools/build-kernel.sh` and `bash tools/build.sh` after updating.

OpenGL in the viewer accelerates host-side display scaling, independently of the experimental guest GPU bridge. The host mouse pointer stays visible, and hovering does not grab it. For hosts without working OpenGL, use `VM_DISPLAY=gtk,zoom-to-fit=on,show-cursor=on ./run.sh`. Diagnostic VNC is opt-in with `QEMU_VNC=1 ./run.sh`; this selects the software GTK viewer because QEMU cannot combine VNC with this OpenGL context.

`bash tools/ssh.sh` connects to root SSH through `127.0.0.1:2222` using a dedicated local key. There is no password login. When enabled, diagnostic VNC listens at `127.0.0.1:5901`. A local first-boot snapshot can be restored when present, but persistent writable VM storage is still in development.

## Graphics development

A local iOS test process has rendered a verified triangle through Mesa/VirGL on the RTX 4060, reporting OpenGL ES 3.1. The EAGL bridge has reached the home screen through this GPU. An experimental pinned-RAM path removes pixel payloads from the virtual Ethernet link: a 750×1624 repeated-colour test measured median readback of 22 ms versus 125 ms for the previous LZ4 network path, excluding four warm-up frames; all pixel checks passed. This measures readback, not whole-app FPS. Interface stalls, startup cost, general app coverage, and Metal support remain unfinished. AMD and Intel GPUs have not been tested on this host.

Shared-buffer development requires the custom kernel (`bash tools/build-kernel.sh`), locally prepared Mesa 25.0.7 and virglrenderer sources/builds, and `python3 tools/prepare-shared-gpu.py` followed by rebuilding both. Kernel build dependencies include flex, bison, autoconf/automake, ncurses development headers and libuuid development headers. Normal setup currently prepares the software-rendered baseline; it does not provision the complete GPU development toolchain.

Start a development VM with `QEMU_SHARED_RAM=1 ./run.sh`, then run `python3 tools/start-gpu-server.py` and `python3 tools/virgl-relay.py --compressed` in separate terminals. Shared RAM uses a private anonymous `memfd`, preloaded from the pristine boot image. This avoids continuously writing guest memory to the SSD; the earlier regular-file mapping caused severe writeback and stalls. `QEMU_SHARED_RAM=file` retains the disk-backed diagnostic implementation. `python3 tools/test-shared-memory.py` verifies byte-for-byte host/guest sharing. Build/deploy the bridge with `python3 tools/build-gpu-support.py` and `python3 tools/deploy-gpu.py`, then `python3 tools/switch-gpu.py`; `--disable` restores that session's saved injection configuration. These manual steps remain experimental. The host server accepts the upstream `VTEST_RENDERNODE` environment variable for selecting a render device; driver compatibility and fallback still need verification.

`kern.ios_kvm_timer_floor_us` is an experimental CPU timer knob in the custom kernel. Its default is zero, preserving normal deadlines. No timing change is enabled by default, and no performance gain is claimed for it until measured.

Apple firmware, extracted system files and generated device identity data are local inputs and are not distributed in this repository.

## Credits and licenses

- OOSEX GANG and contributors: [HakiOS](https://github.com/oosexgang/hakios-xnu-kernel), imported under `vendor/hakios` from revision `a3384e031eb1e605cf37adaefc19991f04f6274b`. Its kernel port, drivers, loader, userland support and image tools provide the current starting components. Original copyright and license files are retained; their terms apply to their respective components.
- dipa-studio and contributors: [ios-sweet](https://github.com/dipa-studio/ios-sweet), used for the first kernel boot experiment in the separate `../ios16-qemu` workspace.
- Apple: open-source XNU and related components, under their respective original licenses.
- QEMU and its contributors: the virtual machine runtime.
- Mesa and VirGL contributors: graphics implementation and host renderer; sources are fetched locally for development.
- LZ4 contributors: lossless GPU transport compression, imported under `vendor/lz4` from v1.10.0 (`ebb370ca83af193212df4dcbadcc5d87bc0de2f0`); its BSD license is retained.

This project has its own repository and integration work. It is not affiliated with Apple or the upstream projects.
