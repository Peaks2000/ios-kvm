#!/usr/bin/env python3
"""Deploy a GPU smoke test into the running local VM over SSH."""
from pathlib import Path
import subprocess
import tarfile
import sys

root = Path(__file__).resolve().parents[1]
if '--bundled' in sys.argv:
    subprocess.run(['bash', str(root / 'tools/ssh.sh'), '-o', 'BatchMode=yes',
                    'mkdir -p /var/tmp/ios-kvm-gpu && /var/jb/usr/bin/cp '
                    '/redwood/share/gpu-bundle/* /var/tmp/ios-kvm-gpu/'], check=True)
    raise SystemExit(0)
archive = root / '.local/gpu-test.tar'
with tarfile.open(archive, 'w') as tar:
    for path, name in [
        (root / '.local/support/gl_test', 'gl_test'),
        (root / '.local/mesa-virgl-build/src/gallium/targets/osmesa/libOSMesa.8.dylib', 'libOSMesa.8.dylib'),
    ]:
        tar.add(path.resolve(), arcname=name, recursive=False)
    for name in ('libhakios.gl.dylib', 'libios-kvm-gpu.dylib', 'gpu-bench', 'gpu-async-check'):
        path = root / '.local/support' / name
        if path.exists():
            tar.add(path, arcname=name, recursive=False)
with archive.open('rb') as stream:
    subprocess.run(['bash', str(root / 'tools/ssh.sh'), '-C', '-o', 'BatchMode=yes',
                    'mkdir -p /var/tmp/ios-kvm-gpu && /var/jb/usr/bin/tar -xf - -C /var/tmp/ios-kvm-gpu'],
                   stdin=stream, check=True, cwd=root)
