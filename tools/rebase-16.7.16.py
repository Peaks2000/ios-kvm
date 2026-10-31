#!/usr/bin/env python3
"""Update checked support hooks to the user's 20H392 shared-cache addresses.

The symbols and vtable entries were read from the user's IPSW with ipsw dyld
symaddr/dump. Objective-C method addresses were reported by the checked hooks
in the first VM run. Runtime expected-address checks remain enabled.
"""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[1] / 'vendor/hakios'
mapping = {
    0x1d5b44520: 0x1d5b38788,  # AppleCLCDDisplay vtable
    0x1d5b45c58: 0x1d5b39ed0,  # EAGLServer vtable
    0x1d5b3b540: 0x1d5b2f6e8,  # GLESContext vtable
    0x1882f0908: 0x1882f9318,  # IOMFBDisplay::is_ready
    0x1882e0c90: 0x1882e9684,  # IOMFBDisplay::ignore_update_p
    0x1882ddda4: 0x1882e6798,  # IOMFBDisplay::begin_update
    0x1882c8568: 0x1882d0f88,  # IOMFBDisplay::finish_update
    0x1882e2548: 0x1882eaf3c,  # EAGLServer::renderer
    0x18835770c: 0x1883601dc,  # EAGLServer::render_update
    0x1884a9308: 0x1884b2164,  # GLContext::function_supported
    0x1884a9350: 0x1884b21ac,  # GLContext::function_uniform
    0x1883fc808: 0x1884053fc,  # Context::compile_shader
    0x1c3ea6298: 0x1c4c699f8,  # +[MCMUserIdentityCache personasAreSupported]
}
log = (root / 'logs/boot-local.log').read_text(errors='replace')
for found, expected in re.findall(
        r'hakios: [+-]\[[^\]]+\]: found (0x[0-9a-f]+), expected (0x[0-9a-f]+); left alone', log):
    mapping[int(expected, 16)] = int(found, 16)
for name in ('inject.c', 'inject_sb.c', 'inject_media.c'):
    path = root / 'userland/src' / name
    source = path.read_text()
    result = re.sub(r'0x[0-9a-fA-F]+UL',
                    lambda m: f'0x{mapping[int(m[0][:-2], 16)]:x}UL'
                    if int(m[0][:-2], 16) in mapping else m[0], source)
    path.write_text(result)
    print(f'{name}: updated firmware-specific addresses; runtime checks retained')
