# Local development status

Target: boot the user's iPhone X iOS 16.7.16 (20H392) into a usable interface, with SSH, Sileo, GPU acceleration and reliable Ethernet. Work is active; the target is not complete.

## Verified on 2026-10-31

- The IPSW manifest identifies iPhone10,3/iPhone10,6 and build 20H392.
- The independently built UEFI loader boots the imported HakiOS XNU kernel and a system image made locally from that IPSW.
- Apple's real launchd runs as PID 1 and starts SpringBoard, backboardd and other system services.
- The virtio Ethernet driver attaches en0. configd obtains 10.0.2.15/24 through DHCP, with router 10.0.2.2 and DNS 10.0.2.3. DNS resolution and an HTTPS request returning status 200 were observed.
- Public-key root SSH works through 127.0.0.1:2222. `uname`, `id`, `ps`, and `dpkg-query` were executed remotely.
- OpenSSH 9.7p1 and Sileo 2.5.1 are installed. Sileo's UI and package installation remain unverified.
- A subsequent run reaches userspace with four guest CPUs. Long-term SMP stability and performance remain unverified.
- Firmware-specific support hooks were rebuilt locally using clang/LLD and the Theos SDK headers/stubs. Expected-address checks remain in place. The four-core run renders the Apple logo and progress bar in software.

## Current run

- Workspace: `/home/finn/Downloads/ios-vm`.
- Current VM: `vendor/hakios/logs/boot-support-4cpu.log`, launched by `bash tools/run.sh 1800`; bounded session, not a permanent daemon. Tool session handle at launch: 71096 (revalidate before acting).
- VNC: 127.0.0.1:5901. QEMU monitor/QMP: 127.0.0.1:4444/4445. SSH: 127.0.0.1:2222.
- Screenshot `vendor/hakios/logs/boot-support-later.png` shows boot progress around 10%; the home screen is not yet verified.
- Two IOMFB vtable slots moved by 8 bytes in 16.7.16. The source now uses begin_update at +0x178 and finish_update at +0x180, verified against the firmware's vtable. This final slot correction has not yet been rebuilt into the running VM; the prior hooks safely declined those two tracing hooks.
- `tools/build.sh` rebuilds support libraries and the small support image; the large system image is reused when its inputs are unchanged.

## Remaining

1. Finish and verify interface boot, touch/keyboard interaction and opening apps.
2. Verify Sileo can launch and install a package over Ethernet.
3. Add a genuine host-accelerated GPU path. The current Mesa/Adreno implementation targets real Qualcomm hardware; QEMU has no supported GPU path yet. Software rendering is not GPU acceleration.
4. Measure and improve network throughput, latency and reliability. Basic connectivity is already proven, not optimal performance.
5. Add persistent VM data: the current /private/var is a memory filesystem and is recreated on cold boot.

## Provenance

Own repository, initial commit 93056e4. Imported code remains under vendor/hakios, with original notices and licenses; README records the imported revision. No upstream remote is configured here. IPSW, extracted files, SDK, generated images, logs and private SSH keys are ignored by git.
