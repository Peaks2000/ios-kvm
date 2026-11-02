#!/usr/bin/env python3
"""Exercise bounded transport rejection and recovery against a running research server."""
import argparse
import json
from pathlib import Path
import re
import socket
import struct
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--generated', type=Path, required=True)
p.add_argument('--token', type=Path, required=True)
p.add_argument('--port', type=int, required=True)
a = p.parse_args()
magic = 0x4e474c33
wire_version = int(re.search(r"#define NG_VERSION (\d+)", Path(__file__).with_name("protocol.h").read_text())[1])
schema = int(re.search(r'0x([0-9a-f]+)', (a.generated / 'schema.h').read_text())[1], 16)
token = a.token.read_bytes()
assert len(token) == 32
names = sorted(json.loads((a.generated / 'scalar-api.json').read_text()))
opcode = {name: i + 1 for i, name in enumerate(names)}

def read(s, n):
    out = bytearray()
    while len(out) < n:
        b = s.recv(n - len(out))
        if not b:
            raise EOFError('connection closed')
        out += b
    return bytes(out)

def connect(*, secret=token, version=wire_version, fingerprint=schema):
    s = socket.create_connection(('127.0.0.1', a.port), timeout=5)
    s.settimeout(5)
    s.sendall(struct.pack('<IIQ32s', magic, version, fingerprint, secret))
    return s

def authenticated():
    s = connect()
    ack_magic, features = struct.unpack('<II', read(s, 8))
    assert ack_magic == magic and not features & ~1
    return s

def record(op, data=b''):
    return struct.pack('<II', op, len(data)) + data + b'\0' * (-len(data) % 8)

def batch(s, commands, capacity=0, sequence=1, declared=None):
    s.sendall(struct.pack('<IIII', magic, sequence, len(commands) if declared is None else declared, capacity) + commands)

def closed(s):
    try:
        return not s.recv(1)
    except (ConnectionResetError, BrokenPipeError):
        return True

def healthy():
    with authenticated() as s:
        batch(s, record(opcode['glGetError']), 4)
        assert struct.unpack('<IIII', read(s, 16)) == (magic, 1, 0, 4)
        assert read(s, 4) == b'\0' * 4

for label, kwargs in [('wrong token', {'secret': bytes(32)}),
                      ('version mismatch', {'version': 99}),
                      ('previous protocol version', {'version': wire_version - 1}),
                      ('schema mismatch', {'fingerprint': schema ^ 1})]:
    with connect(**kwargs) as s:
        assert closed(s), label
    healthy()
    print('Rejected:', label)

cases = [
    ('oversized batch', b'', 0, 1, (16 << 20) + 1, False),
    ('oversized output', record(opcode['glGetError']), (16 << 20) + 1, 1, None, False),
    ('wrong sequence', record(opcode['glGetError']), 4, 2, None, False),
    ('truncated record', b'\0' * 3, 0, 1, None, True),
    ('unknown opcode', record(0xffffffff), 0, 1, None, True),
    ('truncated scalar', record(opcode['glClearColor'], b'\0'), 0, 1, None, True),
    ('query without output space', record(opcode['glCreateShader'], struct.pack('<Q', 0x8b31)), 0, 1, None, True),
    ('oversized object count', record(0x10000, struct.pack('<QQ', 1, 4097)), 4, 1, None, True),
    ('invalid shader string length', record(0x10002, struct.pack('<QII', 1, 1, 0xffffffff)), 0, 1, None, True),
    ('unbound guest vertex pointer', record(0x10005, struct.pack('<6Q', 0, 2, 0x1406, 0, 0, 0x1234)), 0, 1, None, True),
    ('undersized pixel reply', record(0x1000f, struct.pack('<6Q', 0, 0, 8, 8, 0x1908, 0x1401)), 1, 1, None, True),
    ('half-float image with byte-sized blob', record(0x1000d, struct.pack('<9Q', 0x0de1, 0, 0x881a, 4, 4, 0, 0x1908, 0x140b, 1) + bytes(64)), 0, 1, None, True),
    ('packed RGB type with RGBA format', record(0x1000d, struct.pack('<9Q', 0x0de1, 0, 0x1908, 4, 4, 0, 0x1908, 0x8363, 1) + bytes(32)), 0, 1, None, True),
    ('oversized active uniform name', record(0x1001e, struct.pack('<3Q', 0, 0, 65537)), 16, 1, None, True),
    ('oversized attached shader list', record(0x1001d, struct.pack('<2Q', 0, 4097)), 16, 1, None, True),
    ('truncated compressed texture', record(0x10020, struct.pack('<8Q', 0x0de1, 0, 0x9274, 4, 4, 0, 8, 1) + bytes(7)), 0, 1, None, True),
    ('wrong compressed block size', record(0x10020, struct.pack('<8Q', 0x0de1, 0, 0x9274, 4, 4, 0, 1, 1) + bytes(1)), 0, 1, None, True),
]
for label, commands, capacity, seq, declared, reply in cases:
    with authenticated() as s:
        batch(s, commands, capacity, seq, declared)
        if reply:
            head = struct.unpack('<IIII', read(s, 16))
            assert head[0] == magic and head[2] != 0, (label, head)
        assert closed(s), label
    healthy()
    print('Rejected and recovered:', label)

# Live fence IDs are context-owned, and output bounds precede driver writes.
with authenticated() as owner, authenticated() as foreign:
    batch(owner, record(0x10012, struct.pack('<QQ', 0x9117, 0)), 8)
    assert struct.unpack('<IIII', read(owner, 16)) == (magic, 1, 0, 8)
    fence, = struct.unpack('<Q', read(owner, 8))
    assert fence
    batch(foreign, record(0x10016, struct.pack('<Q', fence)), 1)
    assert struct.unpack('<IIII', read(foreign, 16)) == (magic, 1, 0, 1)
    assert read(foreign, 1) == b'\0'
    batch(foreign, record(0x10014, struct.pack('<QQQ', fence, 0, 0)), 4, sequence=2)
    assert struct.unpack('<IIII', read(foreign, 16))[2] != 0
    assert closed(foreign)
    batch(owner, record(0x10017, struct.pack('<QQQ', fence, 0x9114, 1)), 1, sequence=2)
    assert struct.unpack('<IIII', read(owner, 16))[2] != 0
    assert closed(owner)
healthy()
print('Rejected foreign sync and undersized sync output; recovered after owner exit')

# Enforce the per-context live-fence budget and clean up on owner disappearance.
with authenticated() as s:
    previous = 0
    for sequence in range(1, 1025):
        batch(s, record(0x10012, struct.pack('<QQ', 0x9117, 0)), 8, sequence=sequence)
        assert struct.unpack('<IIII', read(s, 16)) == (magic, sequence, 0, 8)
        token_value, = struct.unpack('<Q', read(s, 8))
        assert token_value > previous
        previous = token_value
    batch(s, record(0x10012, struct.pack('<QQ', 0x9117, 0)), 8, sequence=1025)
    assert struct.unpack('<IIII', read(s, 16))[2] != 0
    assert closed(s)
with authenticated() as s:
    batch(s, record(0x10012, struct.pack('<QQ', 0x9117, 0)), 8)
    assert struct.unpack('<IIII', read(s, 16)) == (magic, 1, 0, 8)
    token_value, = struct.unpack('<Q', read(s, 8))
    assert token_value > previous
healthy()
print('Live-fence limit and owner-exit cleanup passed')

# An owner disappearing midway through a blob must release its connection/context.
for _ in range(24):
    with authenticated() as s:
        batch(s, b'\0' * 9, declared=65536)
        s.shutdown(socket.SHUT_WR)
        assert closed(s)
healthy()
# An idle context must outlive the twenty-second partial-request I/O timeout.
with authenticated() as s:
    time.sleep(21)
    batch(s, record(opcode['glGetError']), 4)
    assert struct.unpack('<IIII', read(s, 16)) == (magic, 1, 0, 4)
    assert read(s, 4) == b'\0' * 4
healthy()
print('Idle context survived partial-request timeout')
print('Native GL transport check PASSED (authentication, bounds, malformed blobs, disconnects, recovery)')
