#!/usr/bin/env python3
"""Make the small boot volume for a disk-backed system root; firmware stays on its own disk."""
from pathlib import Path
import shutil
import subprocess

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
subprocess.run(['python3', str(root / 'vendor/hakios/tools/apfs/mkapfs.py'), '-z', '--plain', 'fw',
                '-o', str(root / '.local/disk-boot.apfs'), 'redwood=' + str(stage)], check=True)
