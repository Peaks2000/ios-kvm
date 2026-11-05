#!/usr/bin/env python3
"""Validate shared writes against disposable sealed RAM, never a VM's memory."""
import argparse
import fcntl
import json
import mmap
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--generated', type=Path, required=True)
p.add_argument('--profile', type=Path, required=True)
p.add_argument('--port', type=int, default=6753)
a = p.parse_args()
magic, base = 0x4e474c33, 0x40000000
version = int(re.search(r'#define NG_VERSION (\d+)', Path(__file__).with_name('protocol.h').read_text())[1])
schema = int(re.search(r'0x([0-9a-f]+)', (a.generated / 'schema.h').read_text())[1], 16)
opcodes = {name: i + 1 for i, name in enumerate(sorted(json.loads((a.generated / 'scalar-api.json').read_text())))}
environment = json.loads(a.profile.read_text())['selected']['environment']
fd = os.memfd_create('ios-kvm-ram', os.MFD_ALLOW_SEALING)
os.fchmod(fd, 0o600)
os.ftruncate(fd, 2 << 20)
fcntl.fcntl(fd, fcntl.F_ADD_SEALS, fcntl.F_SEAL_GROW | fcntl.F_SEAL_SHRINK)
ram = mmap.mmap(fd, 2 << 20)
cell, destination = base + 64, base + 4096

def read(s, n):
    out = bytearray()
    while len(out) < n:
        piece = s.recv(n - len(out))
        if not piece:
            raise EOFError('short server reply')
        out += piece
    return bytes(out)

def record(op, data):
    return struct.pack('<II', op, len(data)) + data + bytes(-len(data) % 8)

def scalar(name, *values):
    return record(opcodes[name], struct.pack('<' + 'Q' * len(values), *values))

def floatbits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]

with tempfile.TemporaryDirectory(prefix='native-gl-shared-host-') as directory:
    token = Path(directory) / 'token'
    token.write_bytes(os.urandom(32))
    token.chmod(0o600)
    with (Path(directory) / 'server.log').open('w+') as log:
        server = subprocess.Popen([str((a.generated / 'server').resolve()), str(token), str(a.port)],
                                  env={**os.environ, **environment,
                                       'IOS_KVM_RAM_FILE': f'/proc/{os.getpid()}/fd/{fd}',
                                       'IOS_KVM_RAM_SIZE': str(2 << 20)}, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 10
            while True:
                try:
                    with socket.create_connection(('127.0.0.1', a.port), timeout=.2):
                        break
                except OSError:
                    if server.poll() is not None or time.monotonic() > deadline:
                        log.seek(0)
                        raise RuntimeError(log.read())
                    time.sleep(.05)

            def connect():
                s = socket.create_connection(('127.0.0.1', a.port), timeout=3)
                s.sendall(struct.pack('<IIQ32s', magic, version, schema, token.read_bytes()))
                assert struct.unpack('<II', read(s, 8)) == (magic, 1)
                return s

            def request(*, segments=None, completion=cell, width=1, span=4, stride=4,
                        disconnect=False, invalid_cell=False, expected=0, fmt=0x80e1, dtype=0x1401,
                        asynchronous=False):
                if segments is None:
                    segments = [(destination + 32, 2), (destination, 2)]
                ram[:] = b'\xa5' * len(ram)
                ram[64:72] = bytes(8)
                seq = 37
                args = [0, 0, width, 1, fmt, dtype, stride, span, len(segments), completion, seq]
                commands = scalar('glClearColor', floatbits(1), 0, 0, floatbits(1))
                commands += scalar('glClear', 0x4000)
                commands += record(0x10021, struct.pack('<11Q', *args) +
                                   b''.join(struct.pack('<QQ', *item) for item in segments))
                with connect() as s:
                    capacity = 4 | (0x80000000 if asynchronous else 0)
                    s.sendall(struct.pack('<IIII', magic, 1, len(commands), capacity) + commands)
                    if disconnect:
                        s.shutdown(socket.SHUT_WR)
                    elif asynchronous:
                        # A subsequent query must receive its own sequence, with
                        # no stale acknowledgement from the retained read.
                        query = scalar('glGetError')
                        s.sendall(struct.pack('<IIII', magic, 2, len(query), 4) + query)
                        assert struct.unpack('<IIII', read(s, 16)) == (magic, 2, 0, 4)
                        assert struct.unpack('<I', read(s, 4))[0] == 0
                    elif invalid_cell:
                        header = struct.unpack('<IIII', read(s, 16))
                        assert header[2] != 0
                    else:
                        assert struct.unpack('<IIII', read(s, 16)) == (magic, 1, 0, 4)
                        assert struct.unpack('<i', read(s, 4))[0] == expected
                if disconnect:
                    deadline = time.monotonic() + 3
                    while struct.unpack('<iI', ram[64:72])[1] != seq:
                        assert time.monotonic() < deadline, 'disconnected request never completed'
                        time.sleep(.001)
                if invalid_cell:
                    assert ram[64:72] == bytes(8)
                else:
                    assert struct.unpack('<iI', ram[64:72]) == (expected, seq)
                image = bytearray(b'\xa5' * len(ram))
                image[64:72] = ram[64:72]
                if not expected and not invalid_cell:
                    image[4128:4130] = bytes([0, 0])
                    image[4096:4098] = bytes([255, 255])
                assert ram[:] == image, 'unexpected write outside validated pixel/completion ranges'

            request()
            request(disconnect=True)
            request(asynchronous=True)
            request(asynchronous=True, disconnect=True)
            request(asynchronous=True, width=4097, expected=-22)
            request(asynchronous=True, segments=[(destination, 3), (destination + 2, 1)], expected=-22)
            request(width=4097, expected=-22)
            request(span=5, expected=-22)
            request(fmt=0x1908, dtype=0x140b, expected=-22)
            request(fmt=0x1908, dtype=0x1406, expected=-22)
            request(fmt=0x190a, expected=-22)
            request(segments=[(destination, 3), (destination + 2, 1)], expected=-22)
            request(segments=[(base + (2 << 20) - 1, 4)], expected=-22)
            request(segments=[(base - 1, 4)], expected=-22)
            request(segments=[(cell, 4)], invalid_cell=True)
            request(completion=cell + 1, invalid_cell=True)
            request(completion=base + (2 << 20), invalid_cell=True)
            request()  # An invalid request must not poison later contexts.
            print('Native shared host check PASSED (split/nonlinear SG, bounds, overlaps, disconnect, exact writes)')
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
ram.close()
os.close(fd)
