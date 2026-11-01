#!/usr/bin/env python3
"""Generate typed scalar GLES dispatch from the Khronos registry supplied by Mesa.

Pointer, callback and sync handles require explicit marshalling and are omitted.
Generated prototypes follow the Khronos OpenGL registry; implementation is ours.
"""
import hashlib
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

xml, destination = map(Path, sys.argv[1:3])
root = ET.parse(xml).getroot()
required = set()
for feature in root.findall('feature'):
    if feature.get('api') == 'gles2' and float(feature.get('number')) <= 3.0:
        for group in feature.findall('require'):
            required.update(c.get('name') for c in group.findall('command'))
prototypes = {}
for command in root.findall('./commands/command'):
    proto = command.find('proto')
    if proto is None:
        continue
    name = proto.findtext('name')
    if name not in required:
        continue
    ret = ''.join(proto.itertext()).replace(name, '').strip()
    params = []
    for param in command.findall('param'):
        pname = param.findtext('name')
        text = ''.join(param.itertext())
        params.append((text[:text.rindex(pname)].strip(), pname))
    if any('*' in t or any(x in t for x in ('GLsync', 'GLDEBUG', 'GLegl'))
           for t in [ret, *(t for t, _ in params)]):
        continue
    prototypes[name] = (ret, params)
assert 'glCreateShader' in prototypes and 'glGetError' in prototypes
schema = int.from_bytes(hashlib.sha256(json.dumps(prototypes, sort_keys=True).encode()).digest()[:8], 'little')
destination.mkdir(parents=True, exist_ok=True)
(destination / 'schema.h').write_text(f'#define NG_SCHEMA UINT64_C(0x{schema:016x})\n')
client, server = [], []
for opcode, (name, (ret, params)) in enumerate(sorted(prototypes.items()), 1):
    decl = ', '.join(f'{t} {n}' for t, n in params) or 'void'
    client.append(f'{ret} {name}({decl}) {{')
    client.append(f' uint64_t args[{max(1,len(params))}]={{0}};')
    for index, (ctype, pname) in enumerate(params):
        client.append(f' _Static_assert(sizeof({ctype})<=8,"scalar size"); memcpy(args+{index},&{pname},sizeof({pname}));')
    if ret != 'void':
        client.append(f' {ret} result=0; ng_call({opcode},args,{len(params)*8},&result,sizeof(result)); return result;')
    elif name in ('glFlush', 'glFinish'):
        client.append(f' ng_call({opcode},args,{len(params)*8},NULL,0);')
    else:
        client.append(f' ng_enqueue({opcode},args,{len(params)*8});')
    client.append('}')
    server.append(f'case {opcode}: {{')
    server.append(f' if(size!={len(params)*8}) return -1;')
    for index, (ctype, pname) in enumerate(params):
        server.append(f' {ctype} {pname}; memcpy(&{pname},data+{index*8},sizeof({pname}));')
    call = f'{name}({", ".join(n for _,n in params)})'
    if ret == 'void':
        server.append(f' {call}; return 0;')
    else:
        server.append(f' {ret} value={call}; return ng_output(output,capacity,used,&value,sizeof(value));')
    server.append('}')
(destination / 'client-scalars.h').write_text('\n'.join(client)+'\n')
(destination / 'server-scalars.h').write_text('\n'.join(server)+'\n')
(destination / 'scalar-api.json').write_text(json.dumps(prototypes, indent=2)+'\n')
print(f'Generated {len(prototypes)} typed scalar GLES entry points; schema {schema:016x}')
