#!/usr/bin/env python3
"""Build the experimental native GLES server, guest proxy and diagnostic.

Does not install libraries, alter boot images, or enable the backend in EAGL.
"""
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
src = root / 'tools/native-gl'
out = root / '.local/native-gl-api'
mesa = root / '.local/mesa-25.0.7'
sdk = root / '.local/sdks/iPhoneOS16.5.sdk'
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(src / 'generate.py'),
                str(mesa / 'src/mapi/glapi/registry/gl.xml'), str(out)], check=True)
flags = ['-O2', '-Wall', '-Wextra', '-Werror',
         '-I', str(src), '-I', str(out)]
libs = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'egl', 'glesv2'], text=True))
subprocess.run(['cc', *flags, str(src / 'server.c'), '-o', str(out / 'server'), *libs, '-pthread'], check=True)
subprocess.run(['cc', *flags, str(src / 'gpu-frame-check.c'), '-o', str(out / 'gpu-frame-check'), *libs, '-pthread'], check=True)
clang = ['clang', '-target', 'arm64-apple-ios16.0', '-isysroot', str(sdk),
         '-fuse-ld=lld', *flags, '-I', str(mesa / 'include')]
library = out / 'libios-kvm-native-gl.dylib'
subprocess.run([*clang, str(src / 'guest-exec.c'), '-o', str(out / 'guest-exec')], check=True)
subprocess.run([*clang, str(src / 'display-state.c'), '-o', str(out / 'display-state')], check=True)
subprocess.run([*clang, '-dynamiclib', '-install_name', '@rpath/libios-kvm-native-gl.dylib',
                str(src / 'client.c'), '-framework', 'IOKit', '-o', str(library)], check=True)
for name in ('guest-check', 'context-race-check', 'exit-check'):
    subprocess.run([*clang, str(src / (name + '.c')), str(library), '-framework', 'IOSurface',
                    '-framework', 'CoreFoundation', '-Wl,-rpath,@executable_path',
                    '-o', str(out / name)], check=True)
print(f'Built native GLES research backend in {out}; launcher unchanged')
