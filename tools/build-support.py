#!/usr/bin/env python3
"""Build the modified user-space support using clang/LLD and a local SDK."""
import pathlib
import subprocess
import shutil

root = pathlib.Path(__file__).resolve().parents[1]
sdk = root / '.local/sdks/iPhoneOS16.5.sdk'
out = root / '.local/support'
out.mkdir(parents=True, exist_ok=True)
src = root / 'vendor/hakios/userland/src'
mini = root / 'vendor/hakios/userland/libmini'
linker = root / 'vendor/hakios/xnu-deps/host/bin/ld64'
if linker.exists():
    subprocess.run(['clang', '-target', 'arm64-apple-ios16.0', '--ld-path=' + str(linker), '-Os',
                '-static', '-nostdlib', '-ffreestanding', '-fno-stack-protector', '-fno-builtin',
                '-I', str(mini), '-Wl,-e,_start', '-Wl,-pagezero_size,0x100000000',
                str(src / 'init.c'), str(mini / 'mini.c'), str(mini / 'start.S'),
                '-o', str(out / 'init')], check=True)
    headers = subprocess.check_output(['llvm-objdump', '--macho', '--private-headers', str(out / 'init')], text=True)
    if 'LC_LOAD_DYLINKER' in headers or 'LC_LOAD_DYLIB' in headers or 'LC_UNIXTHREAD' not in headers:
        raise SystemExit('Early init must be fully static with a UNIXTHREAD entry point')
else:
    shutil.copyfile(root / 'vendor/hakios/prebuilt/userland/init', out / 'init')
    print('Using baseline prebuilt init; build-kernel.sh prepares the linker needed for persistent init')
subprocess.run(['clang', '-target', 'arm64-apple-ios16.0', '-isysroot', str(sdk),
                '-fuse-ld=lld', '-O2', str(root / 'tools/vm-session-init.c'),
                '-o', str(out / 'vm-session-init')], check=True)
common = ['clang', '-target', 'arm64-apple-ios16.0', '-isysroot', str(sdk),
          '-fuse-ld=lld', '-O1', '-dynamiclib']
for lib, sources, frameworks in (
    ('libhakios.dylib', ['inject.c'], ['CoreFoundation', 'IOKit']),
    ('libhakios.sb.dylib', ['inject_sb.c', 'ui_layout.c'], ['CoreFoundation', 'IOKit']),
    ('libhakios.app.dylib', ['inject_app.c', 'ui_layout.c'], []),
    ('libhakios.tweaks.dylib', ['inject_tweaks.c', 'ui_layout.c'], ['CoreFoundation']),
    ('libhakios.media.dylib', ['inject_media.c', 'camera_device.c', 'camera_isp.c'],
     ['CoreFoundation', 'CoreMedia', 'CoreVideo', 'IOSurface']),
):
    command = common + ['-install_name', '/redwood/lib/' + lib, '-o', str(out / lib)]
    for framework in frameworks:
        command += ['-framework', framework]
    command += ['-lobjc'] + [str(src / s) for s in sources]
    subprocess.run(command, check=True)
    print(out / lib)
