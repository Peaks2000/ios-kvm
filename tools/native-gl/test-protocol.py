#!/usr/bin/env python3
"""Exercise bounded transport rejection and recovery against a running research server."""
import argparse
import json
from pathlib import Path
import re
import socket
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--generated', type=Path, required=True)
p.add_argument('--token', type=Path, required=True)
p.add_argument('--port', type=int, required=True)
a = p.parse_args()
magic = 0x4e474c33
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

def connect(*, secret=token, version=1, fingerprint=schema):
    s = socket.create_connection(('127.0.0.1', a.port), timeout=5)
    s.settimeout(5)
    s.sendall(struct.pack('<IIQ32s', magic, version, fingerprint, secret))
    return s

def authenticated():
    s = connect()
    assert read(s, 4) == struct.pack('<I', magic)
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

# An owner disappearing midway through a blob must release its connection/context.
for _ in range(24):
    with authenticated() as s:
        batch(s, b'\0' * 9, declared=65536)
        s.shutdown(socket.SHUT_WR)
        assert closed(s)
healthy()
print('Native GL transport check PASSED (authentication, bounds, malformed blobs, disconnects, recovery)')
