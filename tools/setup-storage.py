#!/usr/bin/env python3
"""Create a new journaled data disk; never reformat an existing image."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
disk = root / '.local/data.hfs.img'
disk.parent.mkdir(exist_ok=True)
if disk.exists():
    print(f'Keeping existing data disk: {disk}')
    raise SystemExit(0)
if not (root / 'vendor/hakios/hakios-kernel').is_file():
    raise SystemExit('Build the HFS-capable kernel first: bash tools/build-kernel.sh')
mkfs = shutil.which('mkfs.hfsplus')
if not mkfs:
    raise SystemExit('Install hfsprogs (mkfs.hfsplus) to create the data disk')
fd, temporary = tempfile.mkstemp(prefix='data.hfs.', dir=disk.parent)
try:
    os.fchmod(fd, 0o600)
    os.ftruncate(fd, 8 << 30)
    os.close(fd)
    subprocess.run([mkfs, '-s', '-J', '-U', '0', '-G', '0', '-M', '755',
                    '-v', 'ios-kvm-data', temporary], check=True)
    # Atomic creation with no overwrite even if two setup commands race.
    os.link(temporary, disk)
    print(f'Created private 8 GiB sparse data disk: {disk}')
finally:
    os.unlink(temporary)
