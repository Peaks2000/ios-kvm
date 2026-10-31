#!/usr/bin/env python3
"""Run the experimental GPU server against this VM's actual shared RAM mapping."""
import json
import os
from pathlib import Path
import socket
import time

root = Path(__file__).resolve().parents[1]
instance = root / 'vendor/hakios/qemu/instance'
number = int(instance.read_text().strip()) if instance.exists() else 0
deadline = time.monotonic() + 30
while True:
    try:
        connection = socket.create_connection(('127.0.0.1', 4445 + 10 * number), timeout=2)
        break
    except OSError:
        if time.monotonic() >= deadline:
            raise
        time.sleep(0.2)
with connection as sock:
    with sock.makefile('rwb') as channel:
        channel.readline()
        def qmp(command, arguments=None):
            channel.write(json.dumps({'execute': command, 'arguments': arguments or {}}).encode() + b'\n')
            channel.flush()
            while True:
                reply = json.loads(channel.readline())
                if 'return' in reply: return reply['return']
                if 'error' in reply: raise RuntimeError(reply['error'])
        qmp('qmp_capabilities')
        if not qmp('qom-get', {'path': '/objects/ram', 'property': 'share'}):
            raise SystemExit('Start the VM with QEMU_SHARED_RAM=1 first')
        path = qmp('qom-get', {'path': '/objects/ram', 'property': 'mem-path'})
        size = qmp('qom-get', {'path': '/objects/ram', 'property': 'size'})
os.environ['IOS_KVM_RAM_FILE'] = path
os.environ['IOS_KVM_RAM_SIZE'] = str(size)
server = root / '.local/virgl-build/vtest/virgl_test_server'
os.execv(server, [str(server), '--use-egl-surfaceless', '--socket-path', '/tmp/ios-kvm-virgl.sock'])
