#!/usr/bin/env python3
"""Build the same deterministic CPU workloads for iOS and a native reference."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from tcg_workload import many_targets

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--targets', type=int, choices=(1024, 4096), default=4096)
args = parser.parse_args()
out = root / '.local/guest-tcg-bench'
out.mkdir(parents=True, exist_ok=True)
generated = many_targets(out, args.targets)
source = root / 'tools/tcg-cpu-bench.c'
extra = ['-O2', '-Wall', '-Wextra', '-Werror', '-DIOS_KVM_MANY_TARGETS', '-I', str(out)]
native = out / 'native'
guest = out / 'tcg-cpu-bench'
subprocess.run(['cc', *extra, str(source), '-o', str(native)], check=True)
subprocess.run(['clang', '-target', 'arm64-apple-ios16.0', '-isysroot',
                str(root / '.local/sdks/iPhoneOS16.5.sdk'), '-fuse-ld=lld',
                *extra, str(source), '-o', str(guest)], check=True)
reference = subprocess.check_output([str(native)], text=True, timeout=90)
(out / 'native-reference.log').write_text(reference)
checksums = {}
for name, checksum in re.findall(r'workload=(\w+).*checksum=(\d+)', reference):
    value = int(checksum)
    if name in checksums and checksums[name] != value:
        raise RuntimeError(f'Non-deterministic native checksum: {name}')
    checksums[name] = value
if set(checksums) != {'dependent', 'indirect', 'memory', 'many_targets'}:
    raise RuntimeError('Incomplete native reference')
receipt = {'schema': 1, 'targets': args.targets, 'checksums': checksums,
           'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
           'generated_sha256': hashlib.sha256(generated.read_bytes()).hexdigest(),
           'guest_sha256': hashlib.sha256(guest.read_bytes()).hexdigest()}
(out / 'build.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(guest)
