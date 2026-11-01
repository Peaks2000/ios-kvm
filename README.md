# ios-kvm 

# (not actually based on kvm it is named after another ponect called osx-kvm)

Phone X iOS 16.7.16 (20H392) firmware in QEMU on Linux.

## Local use

Place your own iPhone X iOS 16.7.16 IPSW in `firmware/`, then run `./setup.sh`;  use `./setup.sh /path/to/your.ipsw` Please only use this if you own an iPhone X 

`./run.sh` starts QEMU's native GTK display with zoom-to-fit, four guest CPUs and 9 GiB mapped guest RAM, w Set `QEMU_SMP`, `QEMU_RAM_GB` or `VM_DISPLAY` to change these. ARM-on-x86 uses TCG; the CPU is emulated. 

## warning
Only tested on Fedora Linux with a  Ryzen 7 7435HS, 16 GiB RAM, RTX 4060.

`bash tools/ssh.sh` connects to root SSH through `127.0.0.1:2222` using a local key. There is no password login for VNC 

## Graphics development

A local iOS test process has rendered a verified triangle through Mesa/VirGL on the RTX 4060, reporting OpenGL ES 3.1. The EAGL bridge has also created a hardware-backed window-server context and submitted frames. This is not yet a fast or complete desktop graphics implementation: copying full-resolution pixels back into guest IOSurfaces is a bottleneck, app coverage is unverified, and Metal support is unfinished. The default boot keeps the known software renderer until this integration is validated.

Apple firmware, extracted system files and generated device identity data are local inputs and are not distributed in this repository.

## Credits

- OOSEX GANG and contributors: [HakiOS](https://github.com/oosexgang/hakios-xnu-kernel), imported under `vendor/hakios` from revision `a3384e031eb1e605cf37adaefc19991f04f6274b`. Its kernel port, drivers, loader, userland support and image tools provide the current starting components. Original copyright and license files are retained; their terms apply to their respective components.
- dipa-studio and contributors: [ios-sweet](https://github.com/dipa-studio/ios-sweet), used for the first kernel boot experiment in the separate `../ios16-qemu` workspace.
- Apple: open-source XNU and related components, under their respective original licenses.
- QEMU and its contributors: the virtual machine runtime.
- Mesa and VirGL contributors: graphics implementation and host renderer; sources are fetched locally for development.
- LZ4 contributors: lossless GPU transport compression, imported under `vendor/lz4` from v1.10.0 (`ebb370ca83af193212df4dcbadcc5d87bc0de2f0`); its BSD license is retained.

