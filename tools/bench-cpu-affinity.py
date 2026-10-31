#!/usr/bin/env python3
"""Compare host scheduling with one distinct physical core per QEMU vCPU, then restore."""
import json
import os
from pathlib import Path
import socket
import subprocess

root = Path(__file__).resolve().parents[1]
instance = root / 'vendor/hakios/qemu/instance'
number = int(instance.read_text().strip()) if instance.exists() else 0
with socket.create_connection(('127.0.0.1', 4445 + 10 * number), timeout=5) as sock:
    with sock.makefile('rwb') as channel:
        channel.readline()
        def qmp(command):
            channel.write(json.dumps({'execute': command}).encode() + b'\n')
            channel.flush()
            while True:
                result = json.loads(channel.readline())
                if 'return' in result: return result['return']
                if 'error' in result: raise RuntimeError(result['error'])
        qmp('qmp_capabilities')
        threads = [c['thread-id'] for c in sorted(qmp('query-cpus-fast'), key=lambda c: c['cpu-index'])]
original = {tid: os.sched_getaffinity(tid) for tid in threads}
allowed = set.intersection(*original.values())
cores = {}
for cpu in sorted(allowed):
    topology = Path(f'/sys/devices/system/cpu/cpu{cpu}/topology')
    key = ((topology / 'physical_package_id').read_text().strip(),
           (topology / 'core_id').read_text().strip())
    cores.setdefault(key, cpu)
if len(cores) < len(threads):
    raise SystemExit('Not enough allowed physical cores for this comparison')
chosen = list(cores.values())[-len(threads):]
try:
    for phase, pin in enumerate((False, True, False, True)):
        for tid, cpu in zip(threads, chosen):
            os.sched_setaffinity(tid, {cpu} if pin else original[tid])
        print(f'CPU_AFFINITY_PHASE {phase} PINNED {int(pin)} CORES {chosen}', flush=True)
        subprocess.run(['bash', str(root / 'tools/ssh.sh'), '-o', 'BatchMode=yes',
                        '-o', 'ConnectTimeout=30', '/var/tmp/ios-kvm-gpu/cpu-bench'], check=True)
finally:
    for tid, cpus in original.items():
        try: os.sched_setaffinity(tid, cpus)
        except ProcessLookupError: pass
