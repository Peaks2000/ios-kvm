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

## Update: shared-RAM GPU transfer and visible cursor (2026-10-31, ~12:47 UTC)

User's active goal now includes fast ARM CPU emulation/boot, fast real GPU acceleration, AMD and Intel GPU compatibility, rare software fallback, and a target of outperforming an iPhone X. The latter is NOT demonstrated or promised; current host remains x86 TCG with NVIDIA only. No completion claim.

- Default native viewer now requests GTK OpenGL scaling, zoom-to-fit, visible host cursor, and no grab on hover. QEMU help and live argv confirm supported settings. VNC must be opt-in (`QEMU_VNC=1`) because QEMU rejects VNC alongside this GL context. Software GTK remains selectable with VM_DISPLAY. Native GTK previously sampled in Pixman's expensive separable-convolution scaling under the main QEMU thread.
- Default density is now 2x (750x1624), preserving 375x812 logical size; 3x remains selectable with IOS_KVM_SCALE=3. Loader sets actual ramfb mode/stride consistently. Launch validates 2|3, loader matches the complete fbscale token. 1 GiB translation cache eliminated observed TB flushes in the sampled run, but did not alone fix speed.
- Built our own kernel from pinned Apple XNU + imported patches. `tools/build-kernel.sh` reproduces the local stages, verifies the downloaded x86_64 Theos toolchain archive, selects qemu_defconfig, updates our source changes, and uses upstream make-kernel.sh's explicit symbol-set failure classification. Those known DriverKit symbol-set failures are expected; compile/link failures are not ignored. Installed ncurses-devel and libuuid-devel for the host build. Clean-machine build remains untested.
- MiniIOSurface user client has opt-in selectors 0x100/0x101 to pin/unpin a bounded buffer in the owning task, export physical segments, and complete/release on unpin or connection close. Enabled only with gpu_shared=1 and checks the calling task owns the connection. Corrected an out-of-line reply bug: descriptor outputs must update structureOutputDescriptorSize without setting in-band structureOutputSize.
- QEMU_SHARED_RAM=1 creates a mode-0600, per-instance reflink/sparse clone and maps it share=on. The pristine ram.img stays unchanged. Clone replacement is atomic through a fresh temporary inode so old host mappings cannot point into a newly started VM. Normal launches remain share=off unless requested.
- `tools/test-shared-memory.py`: host wrote 3,145,745 bytes through 97 exported segments; guest checked every byte, zero mismatches, unpin and repin both returned 0. Includes unaligned first/last bytes and >4 KiB out-of-line metadata. The --put option uses this channel to place GPU-development files in /var/tmp/ios-kvm-gpu; uploaded updated 14 MiB Mesa this way, avoiding slow bulk SSH. Upload mode verifies write status, not a file checksum; regular pattern test verifies every byte.
- Experimental vtest private command 64 reads GPU pixels directly into the mapped guest buffer, acknowledges after writes finish, and validates segment counts, lengths, RAM bounds and private owned RAM-file permissions. `tools/prepare-shared-gpu.py` patches the local Mesa and host sources reproducibly. Pin metadata is cached for a Mesa resource and released before its memory is destroyed; pinning anew for every frame was expensive. Host still maps/writes through CPU memory: this removes Ethernet pixel transfer, not all readback/copies and not native GPU passthrough.
- `tools/gpu-bench.c` renders 20 fresh colours and checks sampled pixels for every frame. At 750x1624 on this NVIDIA: excluding first four frames, shared path median readback 21.98 ms (min 15.06, max 46.79), old LZ4/network median 124.54 ms (min 96.99, max 1822.78). All pixel checks and GL errors passed. These are readback benchmarks, NOT whole-app FPS or an iPhone comparison. Logs `.local/gpu-shared-bench.log`, `.local/gpu-lz4-bench.log`.
- Enabled live backboardd GPU bridge. Reached home screen with correctly coloured icons and full bottom dock: `vendor/hakios/logs/shared-desktop-check2.png`. Widgets remain blank, wallpaper black. Earlier captures were spinner/black while respringing; respring still took minutes. Desktop EAGL logs show about 16–26 ms copying frames out, with other costs/occasional stalls. Settings tap was sent; outcome still needs inspecting (`shared-gpu-settings.png`).
- Custom kernel run 2 panicked in scheduler `_pset0` ticket lock after a ~1-second deadline while Sileo/background services were active; no GPU benchmark had completed. Saved log `boot-shared-kernel2.log`, `.local/panic-registers.txt`. Cause not proven. Next run uses explicit `slto_us=60000000 tlto_us=30000000` for diagnosis; has remained live so far. This is not a proven lock fix.
- CPU PC sampling: 22/140 samples in userspace; many others in timebase, timer priority queues and scheduler paths. Small dependent-integer baseline in tools/cpu-bench.c exists; cannot infer general CPU performance from it. Baseline `.local/cpu-bench-baseline.log`. Need repeatable, longer workloads and real before/after measurements.
- Added a runtime timer-floor experiment (kern.ios_kvm_timer_floor_us, 0..10000; default 0 preserves timing) and tools/apply-cpu-tuning.py. Latest kernel built successfully, but this new timer-enabled kernel is NOT the one currently booted and the experiment has NOT been measured. No default timing change is enabled.
- GPU bundle can be added to the support image with IOS_KVM_BUNDLE_GPU=1 bash tools/build.sh; deploy-gpu.py --bundled copies locally from it. Current support image ~103 MiB and includes the earlier shared-capable Mesa; latest resource-pin-cache Mesa was uploaded live and needs re-bundling before reboot. Automatic GPU-at-boot orchestration still pending.
- AMD/Intel GPUs have NOT been tested. Host vtest supports --rendernode / VTEST_RENDERNODE; portable backend selection, driver checks, and verified rare software fallback are still required. Metal and general graphics-intensive app coverage remain unfinished.

### Live handles at this update (revalidate)

- VM session 64979, PID 684938, `boot-shared-kernel3.log`, shared RAM + GTK GL/cursor, four cores, 9 GiB map, 1 GiB TB cache, long diagnostic spinlock deadlines; no timer-floor changes in this running kernel.
- Host vtest session 6641, launched tools/start-gpu-server.py, log `.local/virgl-shared-server3.log`. Compressed relay PID 426351, TCP localhost:6712; old raw relay also exists. Server discovers actual RAM path/size through QMP. Current shared file qemu/ram.session.0.img.
- Latest kernel build `.local/kernel-timer-build.log` successful; generated hakios-kernel includes runtime knob but current live VM predates it.
- Public repo already exists and initial changes were pushed: https://github.com/Peaks2000/ios-kvm . New work in this update is currently uncommitted/unpushed. Private PeaksiOS image repo is still not created.

## Update: CPU experiments and anonymous RAM (2026-10-31, ~13:12 UTC)

- GPU Settings screen verified (`shared-gpu-settings-later.png`), as well as home and full dock. Added optional IOS_KVM_GPU_BOOT=1 with IOS_KVM_BUNDLE_GPU=1 to put the bridge in the boot image's injection map. Root-bundle library fallback and rpath support avoid runtime bulk upload/respring. This development image expects a working local GPU server/relay; automatic lifecycle/fallback is not yet implemented.
- The runtime timer-floor experiment was booted and compared (0,4000,0,4000). Mixed results: median single-thread 127 ms at 0 versus 210 ms at 4000; four-thread 270 versus 171 ms, with large pauses in both. It restored 0 and remains off. These phases were under changing startup load; no general speedup claim. `.local/cpu-timer-comparison.log`.
- Distinct-core host affinity experiment also did not justify enabling pinning: single-thread ~51 ms either way, four-thread ~68 ms unpinned versus 78 ms pinned, with outliers. Restored original affinities. `.local/cpu-affinity-comparison.log`.
- Found a much larger problem in the regular-file shared-RAM mode: QEMU accounted 138,238,320,640 disk-write bytes in roughly nine minutes, from repeated dirty RAM-file writeback. Replaced the normal shared-RAM path with memfd, preloading only allocated extents of pristine ram.img. Mode 0600, fixed-size grow/shrink/seal seals, inherited descriptor held by the launch wrapper, exported via /proc/PID/fd/N. No /dev/shm size cap. QEMU_SHARED_RAM=1 or memfd now selects it; `file` explicitly selects the old diagnostic disk-backed clone. share=off remains the normal software-only path.
- Host vtest supports the memfd path, checking ownership, permissions, mapping bounds and seals. Built successfully. The most recent extra seal check is compiled but the live host server predates that added check; the actual live descriptor was inspected as mode0600, 9 GiB, seals7.
- Memfd pattern test passed: 3,145,745 bytes / 193 segments / zero mismatches, busy duplicate pin and overflowing-range rejection exercised, unpin/repin0. Host file upload through this path also works. CPU samples on the first memfd run: single 53–66 ms, four-thread 84–89 ms, no earlier multi-second pauses; second run had more jitter but no multi-second samples. No controlled claim of higher raw CPU throughput. `.local/cpu-memfd-bench.log`, `cpu-memfd-bench2.log`, `memfd-shared-test.log`.
- QEMU disk writes remained ~10 MB after ~10 minutes with memfd, versus ~138 GB for the prior regular-file run. Host uses zram and significant shared-RAM pages are compressed/swapped; steady vmstat sample showed only small swap-ins, no ongoing swap-out. Further memory reduction remains useful: consider booting system APFS from a readonly virtio disk instead of preloading its entire ~2.4 GiB into RAM. Existing init.c root_apfs support and virtio block driver need inspection/testing before changing that layout.
- First backboardd instance aborted on both cold GPU boots with "lost connection to rendering server"; launchd restarted it and the GPU home screen eventually worked (`memfd-gpu-current.png`). No cause established yet. Found a real transport bug afterwards: zero-length writes emitted an invalid empty frame rejected by our relay. Corrected to a no-op; also reset per-fd read buffers on a new connection and report previously swallowed relay task errors. Host regression test for empty/negative writes, framing, partial reads and fd reuse passes (`cc -O2 -I vendor/lz4 tools/test-gpu-transport.c vendor/lz4/lz4.c -o .local/test-gpu-transport`). Mesa rebuild passes. Cold-boot verification of this latest fix is still pending; a new bundled image is building.
- GPU context create/render/destroy stress passed 10 cycles with correct pixels and GL error0 after the fd-reset fix but before the zero-write fix. `.local/gpu-context-cycles.log`. Source preparation idempotence was checked across Mesa/vtest sources; fixed duplicate prototype insertion.
- Current VM before the pending restart: tool session57711, QEMU PID873786, memfd holder873453, log boot-memfd-gpu.log, four cores, 9 GiB map, 2 GiB TB cache, long diagnostic spinlock deadlines, timer floor0. GPU server tool session2914, log .local/virgl-memfd-server.log. Compressed relay still the earlier process426351 until restarted. Revalidate all handles.
