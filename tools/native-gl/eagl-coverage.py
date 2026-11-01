#!/usr/bin/env python3
"""Compile the existing EAGL bridge and report unresolved native-backend API needs.

Adds missing declarations for the audit object only, never stub implementations.
No libraries are installed into the VM. Use --require-ready as a future build gate.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, default=root / '.local/native-gl-api/eagl-coverage')
p.add_argument('--require-ready', action='store_true')
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
src = root / 'vendor/hakios/userland/src'
library = root / '.local/native-gl-api/libios-kvm-native-gl.dylib'
registry = root / '.local/mesa-25.0.7/src/mapi/glapi/registry/gl.xml'
symbols = subprocess.check_output(['llvm-nm', '--defined-only', '--extern-only', str(library)], text=True)
exports = set(re.findall(r'\b_(gl\w+)$', symbols, re.M))
if not exports:
    raise SystemExit('Build the native GLES library first')
(out / 'exports.txt').write_text('\n'.join(sorted(exports)) + '\n')
result = subprocess.run(['python3', str(root / 'vendor/hakios/userland/mkeagl.py'),
                         str(src / 'eagl_slots.txt'), str(registry), str(out / 'exports.txt'),
                         str(src / 'eagl.c')], capture_output=True, text=True, check=True)
needed = set(re.findall(r'\b(gl[A-Z]\w*)\s*\(', (src / 'eagl.c').read_text()))
prototypes = {}
for command in ET.parse(registry).getroot().findall('./commands/command'):
    proto = command.find('proto')
    if proto is None:
        continue
    name = proto.findtext('name')
    ret = ''.join(proto.itertext()).replace(name, '').strip()
    params = [''.join(v.itertext()) for v in command.findall('param')]
    prototypes[name] = f'extern {ret} {name}({", ".join(params) or "void"});'
missing = sorted(needed - exports)
extra = [prototypes[n] for n in missing if n in prototypes]
(out / 'eagl_gen.h').write_text(result.stdout + '\n#ifdef EAGL_GEN_DECLARATIONS\n' +
                               '\n'.join(extra) + '\n#endif\n')
clang = ['clang', '-Qunused-arguments', '-target', 'arm64-apple-ios16.0',
         '-isysroot', str(root / '.local/sdks/iPhoneOS16.5.sdk'), '-O2', '-I', str(out)]
r = subprocess.run([*clang, '-c', str(src / 'eagl.c'), '-o', str(out / 'eagl.o')],
                   capture_output=True, text=True)
(out / 'compile.log').write_text(r.stdout + r.stderr)
r.check_returncode()
undefined = sorted(set(re.findall(r'\b_(gl\w+)$', subprocess.check_output(
    ['llvm-nm', '--undefined-only', str(out / 'eagl.o')], text=True), re.M)) - exports)
# Compile/link the context adapter separately; this does not assert EAGL is ready.
subprocess.run([*clang, '-fuse-ld=lld', '-Wall', '-Wextra', '-Werror', '-I', str(src),
                '-I', str(root / 'tools/native-gl'), '-dynamiclib',
                '-install_name', '@rpath/libeagl-native-adapter.dylib',
                str(root / 'tools/native-gl/eagl-adapter.c'), str(library),
                '-o', str(out / 'libeagl-native-adapter.dylib')], check=True)
report = {'native_exports': len(exports), 'direct_source_missing': missing,
          'actual_undefined_gl_symbols': undefined, 'dispatch_diagnostics': result.stderr.strip(),
          'scope': 'Compile/link API coverage only; not a desktop compatibility check'}
(out / 'coverage.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
if a.require_ready and (undefined or 'of those QuartzCore calls:' in result.stderr):
    raise SystemExit('Native EAGL API coverage is incomplete; do not enable this backend')
