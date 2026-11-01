# Native GLES backend research

This backend moves GLES processing onto the host CPU/GPU. The guest library
exports 173 GLES entry points without linking guest Mesa. It is experimental
and is **not selected by `run.sh` or installed into EAGL**.

Build with `python3 tools/build-native-gl.py`. The developer build requires the
existing Mesa registry/headers and iOS SDK under `.local`, plus host EGL/GLES
headers, `pkg-config`, a C compiler and Clang/LLD.

Validate against an already running VM:

```sh
python3 tools/native-gl/run-check.py \
  --profile .local/gpu-profile-auto.json \
  --output .local/native-gl-api/my-hardware-check
```

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
- RGBA/BGRA 8-bit texture upload/subimage and readback, preserving row alignment,
  row length, skipped rows/pixels and untouched output padding. BGRA is converted
  on the host, so driver-specific BGRA extensions are not required for transfers.
- Scalar/vector integer queries and stable context-lifetime GL strings.
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

## Integration work remaining

This general backend currently uses bounded copied socket packets. The earlier
shared-RAM fixed-scene prototype's 1.7–8.9x results are **not measurements of this
backend or of desktop/apps**. Recovering that transport requires kernel-owned
holds through host completion, including guest process exit. No raw guest or
host process pointers are exchanged by this version.

A context adapter now compiles separately against the EAGL interface. Audit the
remaining bridge requirements with:

```sh
python3 tools/native-gl/eagl-coverage.py --require-ready
```

This compiles the actual EAGL source and reports unresolved GL symbols and missing
dispatch entries; it currently rejects readiness. Added declarations are for the
audit object only, never stub implementations. No adapter is installed in the VM.

Other missing pieces include complete required API coverage and runtime EAGL
integration, share groups/GLES2 contexts, client vertex/index arrays, PBOs,
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
