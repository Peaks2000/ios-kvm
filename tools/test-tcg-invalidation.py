#!/usr/bin/env python3
"""Check modified-code/remap handling in AArch64 user-mode emulators. Never starts a VM."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('candidate',type=Path)
parser.add_argument('--baseline',default=shutil.which('qemu-aarch64'))
args=parser.parse_args()
if not args.baseline:
    parser.error('Specify an installed AArch64 user-mode --baseline')
emulators=[Path(args.baseline).resolve(),args.candidate.resolve()]
for emulator in emulators:
    version=subprocess.check_output([str(emulator),'--version'],text=True,timeout=5)
    if not version.startswith('qemu-aarch64 version '):
        parser.error('System emulators are rejected; this checks user-mode calculations only')
(root/'.local').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='tcg-invalidation-',dir=root/'.local') as directory:
    binary=Path(directory)/'check'
    subprocess.run(['clang','--target=aarch64-linux-gnu','-O2','-Wall','-Wextra','-Werror',
                    '-ffreestanding','-fno-stack-protector','-nostdlib','-static','-fuse-ld=lld',
                    str(root/'tools/tcg-invalidation-check.c'),'-Wl,-e,_start','-o',str(binary)],check=True)
    for emulator in emulators:
        print(f'User-mode invalidation check: {emulator}',flush=True)
        subprocess.run([str(emulator),'-cpu','cortex-a76',str(binary)],check=True,timeout=60)
