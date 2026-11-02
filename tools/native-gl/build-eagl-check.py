#!/usr/bin/env python3
"""Build a separate-process EAGL macro ABI test, without enabling the desktop backend."""
from pathlib import Path
import json
import re
import subprocess
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
src = root / 'tools/native-gl'
out = root / '.local/native-gl-api'
subprocess.run(['python3', str(src / 'eagl-coverage.py')], cwd=root, check=True)
if not json.loads((out / 'eagl-coverage/coverage.json').read_text()).get('bridge_linked'):
    raise SystemExit('EAGL bridge has unresolved symbols; isolated test cannot use an older artifact')
text = (src / 'eagl-check.m').read_text() + (src / 'eagl-surface-check.h').read_text()
names = set(re.findall(r'\b(gl[A-Z]\w*)\s*\(', text))
slots = {}
for line in (root / 'vendor/hakios/userland/src/eagl_slots.txt').read_text().splitlines():
    values = line.split('#')[0].split()
    if values:
        for name in values[2:]:
            slots[name] = int(values[0], 16)
prototypes = {}
for command in ET.parse(root / '.local/mesa-25.0.7/src/mapi/glapi/registry/gl.xml').getroot().findall('./commands/command'):
    proto = command.find('proto')
    if proto is None:
        continue
    name = proto.findtext('name')
    ret = ''.join(proto.itertext()).replace(name, '').strip()
    params = []
    for param in command.findall('param'):
        pname = param.findtext('name')
        value = ''.join(param.itertext())
        params.append((value[:value.rindex(pname)].strip(), pname))
    prototypes[name] = ret, params
header = ['/* Typed calls through EAGL table(context, arguments), not native GL imports. */']
for name in sorted(names):
    if name not in slots:
        raise SystemExit(f'EAGL macro slot missing: {name}')
    ret, params = prototypes[name]
    decl = ', '.join(f'{t} {n}' for t, n in params) or 'void'
    types = ''.join(', ' + t for t, _ in params)
    args = ''.join(', ' + n for _, n in params)
    header += [f'static {ret} ng_macro_{name}({decl}) {{',
               f' typedef {ret} (*function)(void *{types});',
               f' {"return " if ret != "void" else ""}((function)ng_eagl_table[{slots[name]} / sizeof(void *)])(ng_eagl_table[0]{args});',
               '}', f'#define {name} ng_macro_{name}']
(out / 'eagl-test-dispatch.h').write_text('\n'.join(header) + '\n')
subprocess.run(['clang', '-target', 'arm64-apple-ios16.0', '-isysroot',
                str(root / '.local/sdks/iPhoneOS16.5.sdk'), '-fuse-ld=lld', '-O2',
                '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
                '-I', str(root / '.local/mesa-25.0.7/include'), '-I', str(out),
                '-framework', 'Foundation', '-framework', 'OpenGLES', '-framework', 'IOSurface',
                str(src / 'eagl-check.m'), '-o', str(out / 'eagl-check')], check=True)
print('Built isolated EAGL macro ABI test; desktop unchanged')
