#!/usr/bin/env python3
"""Snapshot the running ios-kvm kernel's loader log, including touch/frame traces."""
import argparse
import json
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--kernel', type=Path, default=root / 'vendor/hakios/hakios-kernel')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
symbols = {}
for line in subprocess.check_output(['llvm-nm', str(args.kernel)], text=True).splitlines():
    match = re.match(r'^([0-9a-fA-F]+)\s+\w\s+(_loader_log(?:_len|_ring_at|_wrapped|_linear)?)$', line)
    if match:
        symbols[match[2]] = int(match[1], 16)
required = ('_loader_log', '_loader_log_len', '_loader_log_ring_at', '_loader_log_wrapped', '_loader_log_linear')
if any(name not in symbols for name in required):
    raise RuntimeError('Kernel is missing the loader trace symbols')
args.output.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='kernel-trace-', dir=root / '.local') as temporary, \
     socket.create_connection(('127.0.0.1', 4445), timeout=10) as sock:
    with sock.makefile('rw') as channel:
        channel.readline()
        def qmp(command, arguments=None):
            channel.write(json.dumps({'execute': command, 'arguments': arguments or {}}) + '\n')
            channel.flush()
            while True:
                reply = json.loads(channel.readline())
                if 'error' in reply:
                    raise RuntimeError(reply['error'])
                if 'return' in reply:
                    return reply['return']
        qmp('qmp_capabilities')
        if qmp('query-name').get('name') != 'ios-kvm':
            raise RuntimeError('QMP does not belong to ios-kvm')
        running = qmp('query-status')['running']
        def read(symbol, size):
            path = Path(temporary) / symbol
            result = qmp('human-monitor-command', {'command-line':
                f'memsave 0x{symbols[symbol]:x} {size} {json.dumps(str(path))}'})
            if result.strip():
                raise RuntimeError(result)
            value = path.read_bytes()
            if len(value) != size:
                raise RuntimeError('Incomplete kernel memory read')
            return value
        try:
            if running:
                qmp('stop')
            data = read('_loader_log', 1536 * 1024)
            length = struct.unpack('<I', read('_loader_log_len', 4))[0]
            ring_at = struct.unpack('<I', read('_loader_log_ring_at', 4))[0]
            wrapped = read('_loader_log_wrapped', 1)[0]
            linear = read('_loader_log_linear', 1)[0]
        finally:
            if running:
                qmp('cont')
head, ring = 256 * 1024, 1024 * 1024
if length > len(data) or ring_at >= ring or wrapped not in (0, 1) or linear not in (0, 1):
    raise RuntimeError('Invalid trace metadata; check that --kernel matches the booted kernel')
if linear or not wrapped:
    output = data[:length]
else:
    output = data[:head] + b'\n[loader trace ring wrapped]\n' + data[head+ring_at:head+ring] + data[head:head+ring_at]
args.output.write_bytes(output)
print(f'Captured {len(output)} bytes; VM resumed' if running else f'Captured {len(output)} bytes; VM remains paused')
