# Native GLES backend research

This backend moves GLES processing onto the host CPU/GPU. The guest library
exports 186 GLES entry points without linking guest Mesa. Diagnostics and the
temporary compositor trial exercise this experimental backend. Normal launches
use VirGL.

Build with `python3 tools/build-native-gl.py`. The developer build requires the
existing Mesa registry/headers and iOS SDK under `.local`, plus host EGL/GLES
headers, `pkg-config`, a C compiler and Clang/LLD.

Validate against an already running VM:

```sh
python3 tools/native-gl/run-check.py \
  --profile .local/gpu-profile-auto.json \
  --output .local/native-gl-api/my-hardware-check
```

Add `--eagl` to also build the complete bridge and run a separate-process test
through EAGLContext methods and the private macro table used by CoreAnimation.
It tests24 spatial frames using modern, explicit GLSL100 and versionless legacy
shaders. It does not switch the window server or prove full compositor behavior.
The bridge preserves modern versioned shaders and converts legacy ones; its
shader fix also passes the same isolated test on the existing Mesa/VirGL backend.

Use a software profile from `tools/gpu_host.py` to check software fallback.
Each check needs a new output directory and a free loopback port (`--port`).
The driver creates a private temporary token, starts an isolated host server,
deploys only temporary guest diagnostic files, runs transport and rendering
checks, then stops its server and removes those files. It does not restart the
VM, change its renderer, replace boot libraries, or modify storage formats.
Receipts identify the actual renderer and binaries. No token is published.

The server follows `VTEST_RENDERNODE` by matching EGL devices to the selected
DRM character device. Explicit software mode uses the selected software EGL
vendor. Intel/AMD hardware validation remains outstanding; using standard
EGL/GLES interfaces alone is not proof that those devices pass.

## Current coverage

- 118 typed scalar GLES 2/3 calls generated from the Khronos registry supplied
  by Mesa; pointer/callback/sync types require separate handling.
- Shader text, compilation/link queries and logs; program/uniform locations;
  buffer uploads/subupdates; generated/deleted GL objects; explicit attribute locations; buffered vertex
  attributes and indexed draws; uniform vectors and square matrices.
- Client vertex arrays are copied into bounded draw packets and uploaded to a
  private native buffer. VAO state, interleaved attributes, normalized byte
  colours, nonzero first/index values, byte/short/int indices, mixed client/VBO
  attributes and buffer-backed indices are covered by twelve rendered frames.
  Caller memory is changed after queueing each draw to verify snapshot lifetime.
  Pointer queries return the original guest value. Limits are32 attributes,
  1024 tracked VAOs, one million vertex/index entries, stride2048 and16MiB
  packets. Client arrays with instanced draws remain an integration task.
- EXT object labels retain their full bounded text per native context, with
  native KHR/core label mirroring where available. Tests cover five object types,
  long and truncated queries, explicit lengths, removal, deletion and GL errors.
  Label storage is limited to4096 entries and4MiB per context, with64KiB per label.
  The adapter follows the EXT label length and query rules.
  [EXT_debug_label specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_debug_label.txt)
- Texture upload/subimage layouts for alpha, luminance, luminance/alpha, red,
  RG, RGB and RGBA bytes; red/RG/RGB/RGBA half/float components; and packed
  RGB565, RGBA4444/5551 and RGBA2101010. Native driver validation determines
  legal internal-format/type combinations. The21 tested layouts are sampled by
  real shaders over63 changing images, including partial subupdates, odd widths,
  eight-byte alignment, skipped rows/pixels and padding. Both direct proxy and
  EAGL macro dispatch are tested. BGRA byte transfers are converted on the host,
  so driver-specific BGRA upload extensions are not required. Other BGRA types,
  integer/depth/stencil pixel transfers and PBO transfers remain unsupported.
- Copied readback includes an explicit GPU success flag (wire version6). Native
  GL errors are preserved, and a failed GPU read leaves caller bytes untouched.
  Shared readback deliberately remains RGBA/BGRA unsigned bytes only; expanded
  upload sizing cannot accidentally permit wider writes into that path.
- Integer/float state queries, shader attachment lists, active uniform names,
  uniform values and texture/vertex/framebuffer metadata. Reflection determines
  scalar/vector/matrix output size and array element locations; a bounded native
  cache is invalidated on program link/delete. Texture-level queries currently
  require native GLES3.1 entry points.
- Bounded compressed2D uploads with checked block layouts (ETC/EAC, ASTC, S3TC,
  RGTC/BPTC, LATC/FXT1 and PVRTC). The host driver must support the format; only
  ETC2 decoding has been validated by rendered pixels. Eight spatial ETC2 frames
  and malformed-block rejection are included in the diagnostic.
- Stable context-lifetime GL strings.
- Native fence creation/deletion, CPU/GPU waits, identity and status queries.
  Guest handles are monotonic numeric IDs scoped to their native context, never
  host pointers. Up to1024 live fences per context; context teardown deletes
  remaining objects. Unknown/stale wait/delete handles currently reject the
  transport; complete invalid-argument GL error behavior remains future work.
  Zero-capacity sync queries preserve guest output storage even on host drivers
  that write one value for a native zero-capacity request.
- Explicit GLES3 context objects own their native connection. Multiple contexts
  can switch on one thread, or migrate after release. A context cannot be current
  on two threads or destroyed while current elsewhere. Pending calls are sent
  before release, including thread exit; the owner retains the context afterward.
  Resource-sharing groups and GLES2 contexts are not yet implemented.

Native EAGL macro calls acquire their context across worker threads, then release
the binding while retaining queued commands. Queries and flushes send that queue.
Context destruction sends remaining commands before closing the connection.
Three concurrent EAGL workers verify program identity and commands surviving
worker exit. This addresses the compositor handoff that previously created
implicit contexts and invalid program IDs.

Enabled-attribute queries use the state tracked by the proxy's VAO enable and
disable entry points. `IOS_KVM_NATIVE_VERTEX_STATE_CACHE=0` requests driver
queries for comparison. `--benchmark-vertex-state` runs driver/cached/cached/
driver blocks through both the proxy and EAGL. Every result is checked against
the enabled state, with32 queries per sample and six measured samples after
one warmup sample per block.

On November5, mean enabled-query latency on the NVIDIA host was3214.12µs
through driver queries and3.51µs through the proxy cache. EAGL macro dispatch
measured3151.64µs and4.00µs respectively. The host used ASan/UBSan with driver
leak detection disabled. Raw timings and binary hashes are recorded in
`evidence/vertex-state-nvidia-20261105.json`. Desktop presentation-rate
comparisons remain a separate verification task.

## Temporary compositor trial

The explicit trial tool deploys a private bundle, changes backboardd's runtime
injection, observes rendering, and restores the saved configuration. It arms a
guest watchdog and confirms its process is alive before switching. Host-side GL
error tracing preserves errors for callers. Bundle hashes and screenshots are
saved with the result.
The trace includes errors raised by proxy validation and errors reported by
the native driver. A120-second November5 trial completed with that broader
trace enabled and restored its saved configuration.

```sh
python3 tools/native-gl/try-compositor.py \
  --profile .local/gpu-profile-auto.json \
  --output .local/native-gl-api/my-compositor-trial --seconds 180
```

The NVIDIA trial in `compositor-handoff-nvidia` reached the home screen and
completed180 seconds using one native context. Its configuration restoration,
temporary-file removal and server shutdown were verified. Early evidence
images had skew and colour stripes caused by QEMU's PPM screenshot encoding.
At width750, it wrote2252 bytes per RGB row while the header described2250.
An ordinary PPM decoder treated padding as pixels. With the VM paused, native
PNG pixels match a stride-corrected PPM byte for byte. The trial tool now asks
QEMU for PNG directly. The capture receipt is
`evidence/capture-stride-20261105.json`. Historical converted screenshots
cannot establish rendering geometry defects.

A fresh90-second NVIDIA asynchronous trial with direct PNG captures shows
correct geometry and colours. A missing lower-right region remains visible,
including clipped search/dock content and absent icons. It restored its saved
configuration and cleaned up. Receipt
`.local/native-gl-api/compositor-png-async-20261105`.
Desktop frame times, gesture behaviour and the missing region require further
work before launcher integration.

A120-second native LLVMPIPE trial rendered the complete icon grid, dock and
search area through the same asynchronous proxy. Some labels/calendar text
were missing, also confirmed by dumping its active guest surface. That trial
used `LP_NUM_THREADS=4`, restored the ordinary backend and cleaned up. Receipt
`.local/native-gl-api/compositor-png-software-20261105`.
These observations narrow the rendering investigation across drivers. They
provide no physical Intel/AMD hardware validation. The capture tool requires
QEMU PNG screendump support.

The guest diagnostic creates its own shaders, textures, vertex/index buffers and
framebuffer. Each rendered frame also checks real native fence completion and
bounded status queries before checking its pixels. It validates every active pixel/channel (tolerance 1) and exact
untouched padding across 20 changing frames, alternating RGBA/BGRA independently.
It also checks shader errors/log bounds, GL error ordering, cached strings and
independent thread contexts, plus full750x1624 RGBA and1125x2436 BGRA
surfaces. It also checks explicit context switching, migration, thread-exit
preservation and recoverable creation failures. A separate32-iteration test
races owner destruction against worker-thread context release. Transport checks exercise authentication/schema
mismatch, length/output bounds, invalid pointer commands, interrupted uploads
and subsequent healthy connections.

The native bridge disables automatic per-call GL error polling by default:
each poll is a synchronous guest/host roundtrip and defeats command batching.
Explicit caller glGetError continues to query the native context. Use
`--eagl-check-every-call` to test the optional diagnostic policy; the runner
checks the bridge actually selected the requested mode. Errors inspected by
bridge diagnostics are saved per context and returned to callers exactly once.
The ordinary Mesa bridge retains its existing diagnostic policy, with the same
error-preservation fix. Both bridges normalize Apple's older HALF_FLOAT_OES
token to HALF_FLOAT for their GLES3 contexts, including IOSurface uploads.
Apple documents the token/format differences in its
[GLES3 migration guide](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/OpenGLES_ProgrammingGuide/AdoptingOpenGLES3/AdoptingOpenGLES3.html).

Add `--guest-uid 501` to run the temporary diagnostics with the compositor's
mobile identity. A separate launcher drops supplementary groups, group and
user IDs before exec; each diagnostic must report its actual identity. Only
the private diagnostic child directory changes ownership, including its0600
token. The launcher stays root-owned in a root-owned parent directory.
The default root test alone does not establish compositor permission behavior.

## Integration work remaining

Commands use bounded copied socket packets. An optional shared readback path
writes completed pixels into sealed guest RAM using physical scatter/gather
ranges from an explicit IOSurface pin. The existing kernel's `0x102` method
retains the kernel-backed surface independently of the requesting task until
the host publishes completion. Wrapped user memory is ineligible. The host
validates RAM ownership/seals, ranges, segment overlaps, completion alignment,
dimensions and output bounds before writing; it preserves row padding and GL
errors. The default shared mode waits for the normal RPC acknowledgement.
`IOS_KVM_NATIVE_ASYNC_READBACK=1` instead sends the retained read and returns
without its socket reply. The host executes each context's stream in order and
publishes success or failure in the existing kernel completion cell. A CPU
consumer must lock the IOSurface before accessing its pixels. Untagged reads,
queries and `glFinish` keep their synchronous contracts. Wire version 8 rejects
older peers, and only batches ending in a retained read may suppress a reply.
The copied fallback remains available before a kernel hold is obtained.
A failed request after obtaining a kernel hold never
falls back to an unretained write. Host loss can leave a bounded retained surface
until VM shutdown; no unsafe cancellation is attempted.

This path permits CPU/GPU overlap, while still reading pixels back into guest
RAM. GPU-resident display presentation remains integration work. Exercise the
opt-in with `run-check.py --shared-ram --async-readback`, or use
`try-compositor.py --async-readback` for the temporary, self-restoring compositor
experiment. The normal launcher continues to select its existing backend.

`run-check.py --shared-ram --async-readback --benchmark-async-readback` compares
synchronous/asynchronous/asynchronous/synchronous retained reads at both phone
resolutions. `elapsed_ns` ends when the readback call returns. `completed_ns`
includes unlocking and reacquiring the IOSurface CPU reader lock, before any
GPU-status query. Both modes validate all fresh pixels and padding after the
measured interval, and verify actual asynchronous submission counts.
The November 5 NVIDIA run measured mean submission times of 11.61/1.29 ms at
750x1624 and 22.35/1.52 ms at 1125x2436 for synchronous/asynchronous modes.
Completed-pixel times were 11.72/9.87 ms and 22.47/19.67 ms, respectively.
The host used ASan/UBSan with driver leak detection disabled. Raw samples and
binary hashes are in `evidence/async-readback-nvidia-20261105.json`.
Desktop frame times and physical AMD/Intel hardware remain unverified here.

Run the retained-surface and isolated EAGL checks with `--shared-ram --eagl`.
This requires a running VM with shared sealed RAM and the matching kernel pin/
completion API, with kernel shared/async support enabled. The server advertises
this feature only when its explicit RAM mapping succeeds; untagged leases and
EAGL `glFinish` use the copied path. Direct IOSurface texture import is still
unsupported. New surface tests check changing spatial images, flipped targets,
padding, prior/new GL errors and kernel error completion.

The process-exit test uses a separately compiled host pause hook, absent from
the normal server. It stops after rendering/validation and before destination
writes, kills the temporary guest producer and confirms that PID is gone. A
separate guest observer finds the pending surface, fails a nonblocking CPU lock,
then waits for host completion. Resuming the host yields correct pixels and
padding; after the observer releases, the surface ID disappears. This passes
on NVIDIA and software rendering with ASan/UBSan hosts (driver leak detection
disabled). The cached-state client also passes on NVIDIA. This tests producer
exit at the paused write boundary, not every possible host/guest failure point.

With `run-exit-check.py --async-readback`, the producer must also report that
submission returned while the host is paused, before it is killed. This checks
that early return and independent surface lifetime hold together.

```sh
python3 tools/native-gl/run-exit-check.py --sanitize \
  --profile .local/gpu-profile-auto.json \
  --output .local/native-gl-api/my-process-exit-check
```

For a disposable host-only bounds test (no VM memory accessed):

```sh
python3 tools/native-gl/test-shared-host.py \
  --generated .local/native-gl-api --profile .local/gpu-profile-auto.json
```

Add `--shared-ram --benchmark` to `run-check.py` for copied/shared/shared/copied
readback measurements in one guest process. Each block has two warmup frames
and eight measured frames. Every image/padding byte is checked outside the
timed region; shared submissions must actually occur. Raw samples, including
stalls, are saved in `benchmark.json`.

On the local Ryzen7435HS/RTX4060 host, an initial paired native clear/readback
test measured mean247.30ms copied versus20.59ms shared at750x1624 (12.01x),
and523.98ms versus26.62ms at1125x2436 (19.68x). Both copied control blocks
were substantially slower than both shared blocks. These are measurements of
this backend's transfer step, **not desktop/app FPS or a whole-VM speedup**.
Receipts: `.local/native-gl-api/shared-phone-benchmark-nvidia`. The earlier
fixed-scene prototype's results are separate. No physical Intel/AMD performance
or near-native execution claim is made.
Public raw timing samples and binary hashes are recorded in
`evidence/shared-readback-nvidia-20261102.json`.

The proxy now shadows valid pack/unpack pixel-store changes per context instead
of issuing five synchronous state queries before each transfer. Invalid writes
still reach the native driver for their errors and leave the shadow unchanged.
PBO binding knowledge is invalidated on relevant binds and buffer deletions;
unknown binding state is queried before transfer. Ordinary public state queries
still come from the driver. PBO transfers themselves remain unsupported.
Independent pack/unpack uploads and reads across two switching contexts are
checked against pixels, padding and actual driver state. Set
`IOS_KVM_NATIVE_STATE_CACHE=0` in a guest diagnostic to restore state queries.

Use `--shared-ram --benchmark-pixel-state` for an independent queried/cached/
cached/queried comparison, with shared readback required throughout. On the
same local host this reduced clear/readback from20.26ms to5.79ms at750x1624
(3.50x), and27.17ms to13.46ms at1125x2436 (2.02x). These are separate paired
measurements; do not multiply them with the earlier transfer ratios to claim
desktop performance. Raw samples/hashes:
`evidence/pixel-state-nvidia-20261102.json`.

The complete bridge now links without unresolved GL symbols. Audit the
remaining bridge requirements with:

```sh
python3 tools/native-gl/eagl-coverage.py --require-ready
```

This compiles the actual EAGL source and reports unresolved GL symbols and missing
dispatch entries; it currently rejects readiness. Added declarations are for the
audit object only, never stub implementations. The remaining marked dispatch
entry is `glAlphaFunc`. The optional isolated test deploys a temporary bridge
inside its own process; no normal boot/runtime library is replaced.

Other missing pieces include complete required API coverage and runtime EAGL
integration, share groups/GLES2 contexts, instanced client arrays, PBOs,
mapped buffers, callbacks, additional pixel types/formats, extension
semantics and full invalid-argument behavior. Currently unsupported calls or
transport failures abort the diagnostic client; this is not suitable for the
window server. Pixel dimensions are bounded to 4096 and packets/replies to
16 MiB. The launcher must not select this backend until its required contracts
and real desktop/app behavior are verified.

Generated prototypes derive from Khronos's OpenGL registry (provided by Mesa);
transport, marshalling, tests and native execution code are part of ios-kvm.
Host EGL/GLES implementations are supplied by the host's graphics drivers.

Sync-query output handling follows [GLES3.1 section4.1.3](https://registry.khronos.org/OpenGL/specs/es/3.1/es_spec_3.1.pdf):
only the requested number of values is copied to the guest. The host queries its
own one-integer storage first, preserving native status and error behavior.
