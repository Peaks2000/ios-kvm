# Experimental QEMU 10.2.2 second-chance jump cache

`qemu-10.2.2-victim-cache.patch` adds a second entry at each existing 12-bit jump-cache index. It checks the displaced entry before falling back to the global translation-block hash table. Both entries retain the original PC, CPU-state and translation-flag validation. Whole-cache, virtual-page and individual-block invalidation clear both entries. It adds about 64 KiB per emulated CPU and leaves the hash/page layout unchanged.

This is a manual experiment, separate from the larger-cache patch. The normal launcher and installed QEMU are unchanged. Apply only to a separate QEMU 10.2.2 source/build tree and keep a known-working emulator:

```bash
# From the separate QEMU source directory:
patch --fuzz=0 -p1 < /path/to/ios-kvm/patches/qemu-10.2.2-victim-cache.patch
# Configure/build QEMU for your host normally, with aarch64-softmmu.
```

Before booting iOS, run the diskless system check from ios-kvm:

```bash
python3 tools/test-tcg-system.py /path/to/baseline/qemu-system-aarch64 /path/to/candidate/qemu-system-aarch64
```

For a separately built `aarch64-linux-user` candidate, run:

```bash
python3 tools/test-tcg-invalidation.py /path/to/candidate/qemu-aarch64 --baseline /path/to/baseline/qemu-aarch64
python3 tools/bench-tcg-builds.py /path/to/candidate/qemu-aarch64 --baseline /path/to/baseline/qemu-aarch64 --targets 4096 --output .local/victim-cpu.json
```

The invalidation checks modify generated code and reuse/remap executable addresses, including two addresses which collide in the 12-bit cache. User-mode checks are not evidence of system-mode MMU correctness; run both kinds. Neither establishes exhaustive SMP correctness.

The first native/LTO user-mode ABBA comparison showed a 7.4% gain in the many-target workload, a 2.1% arithmetic gain, and 1.6%/3.5% losses in the smaller indirect/memory workloads. Results are mixed. A later matched iOS comparison kept the kernel, renderer and four-vCPU configuration unchanged. The warmed many-target workload went from 95.9 to 78.6 ms; the smaller indirect workload worsened from 21.4 to 23.0 ms, while arithmetic/memory were approximately unchanged. Early samples at 120 seconds after launch varied dramatically, so they cannot support steady-performance claims. These are single later CPU sample pairs, not a repeated guest ABBA result.

Two visually verified General-settings scroll runs measured 14.11/14.11 display updates/s on the baseline and 13.98/14.30 on the candidate. There is no demonstrated UI gain. The candidate was not promoted to the default or retained in the live owner VM. Benchmark actual guest workloads using matched compiler settings and a consistent renderer before deciding whether to keep it.
