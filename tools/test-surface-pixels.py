#!/usr/bin/env python3
"""Check the production pixel conversion; --arm executes a standalone calculation, never a VM."""
import argparse
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--arm', action='store_true', help='Also execute a freestanding ARM ELF with QEMU user mode')
args = parser.parse_args()
(root / '.local').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='pixel-check-', dir=root / '.local') as directory:
    work = Path(directory)
    source = root / 'tools/test-surface-pixels.c'
    native = work / 'native'
    subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror',str(source),'-o',str(native)],check=True)
    print('Native pixel conversion:',flush=True)
    subprocess.run([str(native)],check=True)
    if args.arm:
        arm = work / 'arm64'
        subprocess.run(['clang','--target=aarch64-linux-gnu','-O2','-Wall','-Wextra','-Werror',
                        '-ffreestanding','-fno-stack-protector','-nostdlib','-static','-fuse-ld=lld',
                        '-DIOS_KVM_FREESTANDING',str(source),'-Wl,-e,_start','-o',str(arm)],check=True)
        print('Standalone ARM calculation (QEMU user mode, no VM):',flush=True)
        subprocess.run(['qemu-aarch64','-cpu','cortex-a76',str(arm)],check=True)
