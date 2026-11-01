# Native GLES backend research

This backend moves GLES processing onto the host CPU/GPU. The guest library
exports 166 GLES entry points without linking guest Mesa. It is experimental
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
  buffer uploads/subupdates; generated/deleted GL objects; buffered vertex
  attributes and indexed draws; uniform vectors and square matrices.
- RGBA/BGRA 8-bit texture upload/subimage and readback, preserving row alignment,
  row length, skipped rows/pixels and untouched output padding. BGRA is converted
  on the host, so driver-specific BGRA extensions are not required for transfers.
- Scalar/vector integer queries and stable context-lifetime GL strings.
- One isolated native context per proxy thread/connection. The diagnostic checks
  concurrent contexts; context migration and resource-sharing groups are not
  implemented.

The guest diagnostic creates its own shaders, textures, vertex/index buffers and
framebuffer. It validates every active pixel/channel (tolerance 1) and exact
untouched padding across 20 changing frames, alternating RGBA/BGRA independently.
It also checks shader errors/log bounds, GL error ordering, cached strings and
independent thread contexts, plus full750x1624 RGBA and1125x2436 BGRA
surfaces. Transport checks exercise authentication/schema
mismatch, length/output bounds, invalid pointer commands, interrupted uploads
and subsequent healthy connections.

## Integration work remaining

This general backend currently uses bounded copied socket packets. The earlier
shared-RAM fixed-scene prototype's 1.7–8.9x results are **not measurements of this
backend or of desktop/apps**. Recovering that transport requires kernel-owned
holds through host completion, including guest process exit. No raw guest or
host process pointers are exchanged by this version.

Other missing pieces include the EAGL context adapter and complete required API
coverage, share groups/context migration, client vertex/index arrays, PBOs,
mapped buffers, sync objects/callbacks, additional pixel types/formats, extension
semantics and full invalid-argument behavior. Currently unsupported calls or
transport failures abort the diagnostic client; this is not suitable for the
window server. Pixel dimensions are bounded to 4096 and packets/replies to
16 MiB. The launcher must not select this backend until its required contracts
and real desktop/app behavior are verified.

Generated prototypes derive from Khronos's OpenGL registry (provided by Mesa);
transport, marshalling, tests and native execution code are part of ios-kvm.
Host EGL/GLES implementations are supplied by the host's graphics drivers.
