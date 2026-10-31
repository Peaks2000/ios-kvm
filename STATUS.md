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

## Update: native viewer and GPU prototype (2026-10-31, ~12:00 UTC)

- The four-core VM reached the real iOS home screen. Sileo opened and displayed its onboarding UI. QMP absolute touches selected apps. Evidence: `logs/display-input-check.png`, `logs/input-settings-test.png`, `logs/sileo-open-test.png` under vendor/hakios.
- A dedicated GtkVnc viewer was added with aspect-preserving scaling and direct QMP touch input. Fixed a socket lifetime bug and moved pointer RPC off the GTK thread. Current normal launch instead uses QEMU's native GTK display with zoom-to-fit; VNC remains diagnostic.
- Root `run.sh` and `setup.sh` exist. Normal launches have no timeout, default to four guest CPUs and nine GiB mapped RAM (six GiB kernel-managed RAM). Earlier bounded test stopped normally at its 30-minute limit.
- First-boot /private/var was archived over SSH to `.local/var-firstboot.tar` (304 MiB). Tar reported files changing during capture, so it is not an atomic disk backup. A local support image now carries it and rc.vm restores it before launchd. New run reached the home screen again. Wallpaper was white in the first screenshot; rendering/state restoration still needs verification.
- Loader payload reservation is now 128 MiB, resident system-image offset 256 MiB, forced kernel base 0xf8000000 and resident image address 0x108000000. qemu/ram.layout tracks this layout. The 99 MiB personal support image did not fit the previous 64 MiB limit; the first two native restore launches stopped safely in the loader, and the corrected third launch reached userspace.
- Current VM: `vendor/hakios/logs/boot-native-restore3.log`, launch tool handle 74471. Native GTK window named ios-kvm. Revalidate before acting. No GPU bridge enabled in this fresh run yet.

### Actual host GPU evidence

- Built Mesa 25.0.7 for iOS with Gallium VirGL vtest over guest Ethernet to a loopback-only host relay and a locally built virglrenderer 1.3.0 server. vtest protocol 0 avoids passing Linux file descriptors into iOS. VirGL server must use its default per-client fork mode, not --multi-clients (which requires protocol 3).
- Guest `gl_test` reported `virgl (NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2)` and `OpenGL ES 3.1 Mesa 25.0.7`; triangle readback pixels matched expected colors and area, GL error 0.
- Warm 320x640 fill workloads: shortest times about 2.1–2.4 ms. This measures rendering/fence work, not full-frame presentation.
- Full 1125x2436 triangle: initial draw 111 ms / readback 556 ms; LZ4 transport draw 296 ms / readback 205 ms. File `.local/gpu-lz4-test.log` records the compressed test. Initial draw includes shader/context startup and these are not comparable steady-state FPS figures.
- EAGL support was rebuilt against this Mesa. The live window-server injection created a real host-GPU OpenGL ES 3 context, installed all checked hooks, and submitted render frames. Runtime log measured ~108 ms copying a frame out (~10 FPS ceiling before other work), so this is not yet a fast desktop. The screenshot was a respring spinner, not proof of a complete hardware-rendered home screen.
- Fixed a crash: the Adreno-specific IOSurface-ID-as-EGLImage path must never be used by VirGL. VirGL now uses the CPU readback fallback. Dummy 16x16 OSMesa context no longer asks for an unused depth/stencil buffer that triggered unsupported format 19.
- Removed one transfer copy in the vtest path; changed inverted EAGL surface readback from one GL call per row to one bulk read plus a CPU row flip. The latter compiles but is not yet runtime-verified.
- Tools: build-virgl.sh, prepare-virgl-mesa.py, build-gpu-support.py, deploy-gpu.py, switch-gpu.py (--disable restores saved injection map), virgl-relay.py (--compressed listens on 6712; old raw mode 6711). Runtime files go under /var/tmp/ios-kvm-gpu and disappear on reboot. Persistent root integration is still pending.
- Host renderer process launched with `.local/virgl-build/vtest/virgl_test_server --use-egl-surfaceless --socket-path /tmp/ios-kvm-virgl.sock`; raw relay and compressed relay may still run. Revalidate via process/socket state.
- Performance next step: eliminate bulk GPU readback into guest IOSurfaces, preferably a proper shared-memory/IOSurface export path with pinning and synchronization. Compression alone is insufficient for high-entropy graphics. Metal and general app coverage remain unfinished.
- Branding: software/public repo must be ios-kvm. PeaksiOS is only the owner's private portable image store. Two GitHub repositories have not been created yet as of this update.

## Earlier run

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
