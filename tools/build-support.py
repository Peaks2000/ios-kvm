#!/usr/bin/env python3
"""Build the modified user-space support using clang/LLD and a local SDK."""
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
sdk = root / '.local/sdks/iPhoneOS16.5.sdk'
out = root / '.local/support'
out.mkdir(parents=True, exist_ok=True)
src = root / 'vendor/hakios/userland/src'
common = ['clang', '-target', 'arm64-apple-ios16.0', '-isysroot', str(sdk),
          '-fuse-ld=lld', '-O1', '-dynamiclib']
for lib, sources, frameworks in (
    ('libhakios.dylib', ['inject.c'], ['CoreFoundation', 'IOKit']),
    ('libhakios.sb.dylib', ['inject_sb.c'], ['CoreFoundation', 'IOKit']),
    ('libhakios.media.dylib', ['inject_media.c', 'camera_device.c', 'camera_isp.c'],
     ['CoreFoundation', 'CoreMedia', 'CoreVideo', 'IOSurface']),
):
    command = common + ['-install_name', '/redwood/lib/' + lib, '-o', str(out / lib)]
    for framework in frameworks:
        command += ['-framework', framework]
    command += ['-lobjc'] + [str(src / s) for s in sources]
    subprocess.run(command, check=True)
    print(out / lib)
