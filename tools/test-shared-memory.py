#!/usr/bin/env python3
"""Exercise the opt-in shared-RAM kernel API in the running local VM."""
import json
import mmap
import socket
from pathlib import Path
import subprocess
import tarfile
import sys
import shlex

root = Path(__file__).resolve().parents[1]
upload = Path(sys.argv[2]).resolve() if len(sys.argv) == 3 and sys.argv[1] == '--put' else None
instance = root / 'vendor/hakios/qemu/instance'
number = int(instance.read_text().strip()) if instance.exists() else 0
with socket.create_connection(('127.0.0.1', 4445 + 10 * number), timeout=5) as sock:
    with sock.makefile('rwb') as channel:
        channel.readline()
        def qmp(command, arguments=None):
            channel.write(json.dumps({'execute': command, 'arguments': arguments or {}}).encode() + b'\n')
            channel.flush()
            while True:
                reply = json.loads(channel.readline())
                if 'return' in reply:
                    return reply['return']
                if 'error' in reply:
                    raise RuntimeError(reply['error'])
        qmp('qmp_capabilities')
        if not qmp('qom-get', {'path': '/objects/ram', 'property': 'share'}):
            raise SystemExit('Start the VM with QEMU_SHARED_RAM=1 first')
        ram = Path(qmp('qom-get', {'path': '/objects/ram', 'property': 'mem-path'}))
        mapped_size = qmp('qom-get', {'path': '/objects/ram', 'property': 'size'})
out = root / '.local/support/shared-memory-test'
subprocess.run(['clang', '-target', 'arm64-apple-ios16.0', '-isysroot',
                str(root / '.local/sdks/iPhoneOS16.5.sdk'), '-fuse-ld=lld', '-O2',
                '-framework', 'IOKit', '-framework', 'CoreFoundation',
                str(root / 'tools/shared-memory-test.c'), '-o', str(out)], check=True)
archive = root / '.local/shared-memory-test.tar'
with tarfile.open(archive, 'w') as tar:
    tar.add(out, arcname=out.name)
ssh = ['bash', str(root / 'tools/ssh.sh'), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=30']
with archive.open('rb') as stream:
    subprocess.run(ssh + ['mkdir -p /var/tmp/ios-kvm-gpu && /var/jb/usr/bin/tar -xf - -C /var/tmp/ios-kvm-gpu'],
                   stdin=stream, check=True)
command = '/var/tmp/ios-kvm-gpu/shared-memory-test'
if upload:
    command += ' ' + shlex.quote(upload.name) + ' ' + str(upload.stat().st_size)
with subprocess.Popen(ssh + [command], stdout=subprocess.PIPE,
                      text=True) as guest:
    message = json.loads(guest.stdout.readline())
    size = message['bytes']
    segments = message['segments']
    if not 0 < size <= 64 << 20 or sum(n for _, n in segments) != size:
        raise ValueError('Invalid exported buffer size')
    with ram.open('r+b') as file, mmap.mmap(file.fileno(), mapped_size) as memory:
        # Validate every range before modifying the guest's test allocation.
        for physical, length in segments:
            offset = physical - 0x40000000
            if length <= 0 or offset < 0 or offset + length > len(memory):
                raise ValueError('Physical segment is outside mapped guest RAM')
        if upload:
            data = upload.read_bytes() + b'\xa5'
            if len(data) != size:
                raise ValueError('Exported size does not match the input file')
        else:
            pattern = bytes(((i * 17 + 43) & 255) for i in range(256))
            data = (pattern * ((size + 255) // 256))[:size-1] + b'\xa5'
        at = 0
        for physical, length in segments:
            offset = physical - 0x40000000
            memory[offset:offset+length] = data[at:at+length]
            at += length
    print(f'Host wrote {size} bytes through {len(segments)} pinned segments', flush=True)
    print(guest.stdout.read(), end='')
    if guest.wait():
        raise SystemExit('Guest shared-memory validation failed')
