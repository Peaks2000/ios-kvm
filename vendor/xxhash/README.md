This is the xxHash header bundled with Mesa 25.0.7, from the checksum-pinned Mesa archive used by `tools/setup-gpu.py`.

Copyright Yann Collet, BSD 2-Clause license; the full license is retained in `xxhash.h`. ios-kvm replaces the dependency on Mesa's `util/compiler.h` with a standalone `FALLTHROUGH` definition. The hash algorithm is unchanged. The EAGL bridge uses the inline XXH64 function for IOSurface change detection.
