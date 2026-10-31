#!/usr/bin/env python3
"""Measure the fixed CPU workloads inside an already running managed iOS VM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import statistics
import subprocess
import time
from datetime import datetime, timezone

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--label', required=True)
args = parser.parse_args()
build_dir = root / '.local/guest-tcg-bench'
build = json.loads((build_dir / 'build.json').read_text())
binary = build_dir / 'tcg-cpu-bench'
if hashlib.sha256(binary.read_bytes()).hexdigest() != build['guest_sha256']:
    raise RuntimeError('Guest benchmark differs from its reference receipt; rebuild it')
with socket.create_connection(('127.0.0.1', 4445), timeout=5) as sock:
    with sock.makefile('rw') as channel:
        channel.readline()
        def qmp(command):
            channel.write(json.dumps({'execute': command}) + '\n'); channel.flush()
            while True:
                response = json.loads(channel.readline())
                if 'error' in response:
                    raise RuntimeError(response['error'])
                if 'return' in response:
                    return response['return']
        qmp('qmp_capabilities')
        cpus = qmp('query-cpus-fast')
tid = cpus[0]['thread-id']
status = Path(f'/proc/{tid}/status').read_text()
pid = int(re.search(r'^Tgid:\s+(\d+)', status, re.M)[1])
emulator = Path(f'/proc/{pid}/exe').resolve()
emulator_hash = hashlib.sha256(emulator.read_bytes()).hexdigest()
ssh = ['bash', str(root / 'tools/ssh.sh'), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10']
with binary.open('rb') as source:
    subprocess.run(ssh + ['mkdir -p /var/tmp/ios-kvm-bench && '
                         'cat > /var/tmp/ios-kvm-bench/tcg-cpu-bench && '
                         '/var/jb/usr/bin/chmod 755 /var/tmp/ios-kvm-bench/tcg-cpu-bench'],
                   stdin=source, check=True, timeout=30)
started = time.time()
session = json.loads((root / '.local/vm-session.json').read_text())
stamp = Path(session['logs']).name.split('-', 1)[0]
boot_started = datetime.strptime(stamp, '%Y%m%dT%H%M%SZ').replace(tzinfo=timezone.utc).timestamp()
result = subprocess.run(ssh + ['/var/tmp/ios-kvm-bench/tcg-cpu-bench'],
                        capture_output=True, text=True, timeout=180)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.with_suffix('.log').write_text(result.stdout + result.stderr)
result.check_returncode()
rows = re.findall(r'^tcg-cpu: workload=(\w+) sample=(\d+) elapsed_us=(\d+) checksum=(\d+)$',
                  result.stdout, re.M)
if len(rows) != 32:
    raise RuntimeError('Incomplete guest benchmark; see captured log')
medians = {}
for name, expected in build['checksums'].items():
    values = [(int(sample), int(elapsed), int(checksum))
              for work, sample, elapsed, checksum in rows if work == name]
    if sorted(sample for sample, _, _ in values) != list(range(8)):
        raise RuntimeError(f'Missing/duplicate samples: {name}')
    if any(checksum != expected for _, _, checksum in values):
        raise RuntimeError(f'iOS checksum disagrees with native reference: {name}')
    medians[name] = statistics.median(elapsed for sample, elapsed, _ in values if sample >= 2)
receipt = {'schema': 1, 'kind': 'Live iOS CPU workloads; not a UI FPS benchmark',
           'label': args.label, 'started_unix': started, 'elapsed_seconds': time.time() - started,
           'emulator': str(emulator), 'emulator_sha256': emulator_hash, 'vcpus': len(cpus),
           'session_logs': session['logs'], 'boot_age_seconds': started - boot_started,
           'guest_build': build, 'host_load': os.getloadavg(),
           'host_meminfo': Path('/proc/meminfo').read_text(), 'median_us': medians}
args.output.write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(medians, indent=2))
