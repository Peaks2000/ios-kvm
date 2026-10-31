# ios-kvm

An independent project to run the user's iPhone X iOS 16.7.16 (20H392) firmware in QEMU on Linux.

`ios-kvm` is the software project. The separate private `PeaksiOS` repository is for the owner's personal VM image portability. Despite the project name, ARM guests on x86 hosts currently use QEMU TCG CPU emulation, not KVM.

The target is a usable iOS interface, SSH, Sileo, GPU acceleration and reliable virtual Ethernet. The VM currently runs launchd, supports root SSH and DHCP Ethernet with working DNS/HTTPS, and renders the Apple boot progress screen. The home screen, Sileo operation and GPU acceleration remain in development. See [STATUS.md](STATUS.md) for evidence and remaining work.

## Local use

Place your own iPhone X iOS 16.7.16 IPSW in `firmware/`, then run `./setup.sh`; alternatively use `./setup.sh /path/to/your.ipsw`. The setup script extracts your files, prepares the SDK and support programs, and builds the local system images. Apple firmware and generated images are never bundled in the public repository.

`./run.sh` starts QEMU's native GTK display with zoom-to-fit, four guest CPUs and 9 GiB mapped guest RAM, without a session time limit. Set `QEMU_SMP`, `QEMU_RAM_GB` or `VM_DISPLAY` to override these. ARM-on-x86 uses TCG; the CPU is emulated. Current development host: Linux, Ryzen 7 7435HS, 16 GiB RAM, RTX 4060.

`bash tools/ssh.sh` connects to root SSH through `127.0.0.1:2222` using a dedicated local key. There is no password login. VNC at `127.0.0.1:5901` remains available for diagnostics. A local first-boot snapshot can be restored when present, but persistent writable VM storage is still in development.

## Graphics development

A local iOS test process has rendered a verified triangle through Mesa/VirGL on the RTX 4060, reporting OpenGL ES 3.1. The EAGL bridge has also created a hardware-backed window-server context and submitted frames. This is not yet a fast or complete desktop graphics implementation: copying full-resolution pixels back into guest IOSurfaces is a bottleneck, app coverage is unverified, and Metal support is unfinished. The default boot keeps the known software renderer until this integration is validated.

Apple firmware, extracted system files and generated device identity data are local inputs and are not distributed in this repository.

## Credits and licenses

- OOSEX GANG and contributors: [HakiOS](https://github.com/oosexgang/hakios-xnu-kernel), imported under `vendor/hakios` from revision `a3384e031eb1e605cf37adaefc19991f04f6274b`. Its kernel port, drivers, loader, userland support and image tools provide the current starting components. Original copyright and license files are retained; their terms apply to their respective components.
- dipa-studio and contributors: [ios-sweet](https://github.com/dipa-studio/ios-sweet), used for the first kernel boot experiment in the separate `../ios16-qemu` workspace.
- Apple: open-source XNU and related components, under their respective original licenses.
- QEMU and its contributors: the virtual machine runtime.
- Mesa and VirGL contributors: graphics implementation and host renderer; sources are fetched locally for development.
- LZ4 contributors: lossless GPU transport compression, imported under `vendor/lz4` from v1.10.0 (`ebb370ca83af193212df4dcbadcc5d87bc0de2f0`); its BSD license is retained.

This project has its own repository and integration work. It is not affiliated with Apple or the upstream projects.
