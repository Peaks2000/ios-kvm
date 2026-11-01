#!/usr/bin/env python3
"""Build the iOS EAGL-to-VirGL integration and a live-test injection library."""
from pathlib import Path
import re
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
src = root / 'vendor/hakios/userland/src'
out = root / '.local/support'
out.mkdir(parents=True, exist_ok=True)
mesa = root / '.local/mesa-25.0.7'
library = root / '.local/mesa-virgl-build/src/gallium/targets/osmesa/libOSMesa.8.dylib'
symbols = subprocess.check_output(['llvm-nm', '--defined-only', '--extern-only', str(library)], text=True)
for name in ('ios_kvm_readback_lease_create', 'ios_kvm_readback_lease_set', 'ios_kvm_readback_lease_destroy', 'ios_kvm_readback_lease_surface'):
    if not re.search(r'\b_' + name + r'$', symbols, re.M):
        raise SystemExit(f'Mesa is missing {name}; rebuild with tools/build-virgl.sh')
exports = sorted(set(re.findall(r'\b_(gl\w+)$', symbols, re.M)))
if not exports:
    raise SystemExit('Mesa exports no GL entry points')
(out / 'exported-gl.txt').write_text('\n'.join(exports) + '\n')
with (out / 'eagl_gen.h').open('w') as header:
    subprocess.run(['python3', str(root / 'vendor/hakios/userland/mkeagl.py'),
                    str(src / 'eagl_slots.txt'), str(mesa / 'src/mapi/glapi/registry/gl.xml'),
                    str(out / 'exported-gl.txt'), str(src / 'eagl.c')], stdout=header, check=True)
common = ['clang', '-Qunused-arguments', '-target', 'arm64-apple-ios16.0', '-isysroot',
          str(root / '.local/sdks/iPhoneOS16.5.sdk'), '-fuse-ld=lld', '-O2', '-dynamiclib',
          '-Wl,-rpath,/var/tmp/ios-kvm-gpu', '-Wl,-rpath,/redwood/lib']
common += ['-Wl,-rpath,/redwood/share/gpu-bundle']
subprocess.run(common + ['-install_name', '/redwood/lib/libhakios.gl.dylib', '-I', str(out),
                        '-framework', 'CoreFoundation', '-framework', 'IOKit', '-framework', 'IOSurface',
                        '-lobjc', str(library), '-o', str(out / 'libhakios.gl.dylib'),
                        str(src / 'eagl.c'), str(src / 'gl_osmesa.c')], check=True)
subprocess.run(common + ['-DIOS_KVM_VIRGL=1',
                        '-DHAKIOS_GL="/var/tmp/ios-kvm-gpu/libhakios.gl.dylib"',
                        '-install_name', '/var/tmp/ios-kvm-gpu/libios-kvm-gpu.dylib',
                        '-framework', 'CoreFoundation', '-framework', 'IOKit', '-lobjc',
                        '-o', str(out / 'libios-kvm-gpu.dylib'), str(src / 'inject.c')], check=True)
shutil.copyfile(library, out / 'libOSMesa.8.dylib')
subprocess.run(['clang', '-target', 'arm64-apple-ios16.0', '-isysroot',
                str(root / '.local/sdks/iPhoneOS16.5.sdk'), '-fuse-ld=lld', '-O2',
                '-Wl,-rpath,/redwood/share/gpu-bundle', '-framework', 'CoreFoundation',
                '-framework', 'IOKit', str(src / 'gl_test.c'), str(library),
                '-o', str(out / 'gl_test')], check=True)
subprocess.run(['clang', '-target', 'arm64-apple-ios16.0', '-isysroot',
                str(root / '.local/sdks/iPhoneOS16.5.sdk'), '-fuse-ld=lld', '-O2',
                '-I', str(mesa / 'include'), '-Wl,-rpath,@executable_path',
                '-Wl,-rpath,/redwood/share/gpu-bundle',
                str(root / 'tools/gpu-bench.c'), str(library), '-o', str(out / 'gpu-bench')], check=True)

subprocess.run(['clang', '-target', 'arm64-apple-ios16.0', '-isysroot',
                str(root / '.local/sdks/iPhoneOS16.5.sdk'), '-fuse-ld=lld', '-O2',
                '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
                '-I', str(mesa / 'include'), '-Wl,-rpath,@executable_path',
                '-framework', 'CoreFoundation', '-framework', 'IOSurface', '-framework', 'IOKit',
                str(root / 'tools/gpu-async-check.c'), str(library),
                '-o', str(out / 'gpu-async-check')], check=True)
print(f'Built EAGL bridge with {len(exports)} exported GL entry points and GPU checks')
