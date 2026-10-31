# iOS VM

An independent project to run the user's iPhone X iOS 16.7.16 (20H392) firmware in QEMU on Linux.

The target is a usable iOS interface, SSH, Sileo, GPU acceleration and reliable virtual Ethernet. These are development objectives, not claims of working features. The initial experiment reached early XNU startup using ios-sweet; the complete iOS system has not yet booted here.

Apple firmware, extracted system files and generated device identity data are local inputs and are not distributed in this repository.

## Credits and licenses

- OOSEX GANG and contributors: [HakiOS](https://github.com/oosexgang/hakios-xnu-kernel), imported under `vendor/hakios` from revision `a3384e031eb1e605cf37adaefc19991f04f6274b`. Its kernel port, drivers, loader, userland support and image tools provide the current starting components. Original copyright and license files are retained; their terms apply to their respective components.
- dipa-studio and contributors: [ios-sweet](https://github.com/dipa-studio/ios-sweet), used for the first kernel boot experiment in the separate `../ios16-qemu` workspace.
- Apple: open-source XNU and related components, under their respective original licenses.
- QEMU and its contributors: the virtual machine runtime.

This project has its own repository and integration work. It is not affiliated with Apple or the upstream projects.
