#!/usr/bin/env python3
"""Make the small boot volume for a disk-backed system root; firmware stays on its own disk."""
from pathlib import Path
import shutil
import subprocess
import json
import tempfile

root = Path(__file__).resolve().parents[1]
source = root / 'vendor/hakios/userland/root'
stage = root / '.local/disk-boot-root'
if stage.exists():
    shutil.rmtree(stage)
subprocess.run(['cp', '-al', str(source), str(stage)], check=True)
for directory in ('flash', 'private/preboot', 'redwood', 'dev'):
    (stage / directory).mkdir(parents=True, exist_ok=True)
(stage / 'sbin/launchd').unlink(missing_ok=True)
shutil.copy2(root / '.local/support/init', stage / 'sbin/launchd')
with tempfile.TemporaryDirectory(prefix='disk-boot-build-', dir=root / '.local') as directory:
    image = Path(directory) / 'disk-boot.apfs'
    subprocess.run(['python3', str(root / 'vendor/hakios/tools/apfs/mkapfs.py'), '-z', '--plain', 'fw',
                    '-o', str(image), 'redwood=' + str(stage)], check=True)
    image.replace(root / '.local/disk-boot.apfs')
gpu_boot = any('libios-kvm-gpu.dylib' in line.split()[0] and 'backboardd' in line.split()[1:]
               for line in (source / 'etc/inject.conf').read_text().splitlines() if line.split())
manifest = root / '.local/boot-features.json'
temporary = manifest.with_suffix('.tmp')
temporary.write_text(json.dumps({'schema': 1, 'disk_system': True, 'gpu_boot': gpu_boot}) + '\n')
temporary.replace(manifest)
