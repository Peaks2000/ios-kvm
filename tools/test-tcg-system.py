#!/usr/bin/env python3
"""Test TCG system-mode MMU invalidation in a disposable 128 MiB diskless machine."""
import argparse
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('emulators', nargs='+', type=Path)
args = parser.parse_args()
emulators = [p.resolve() for p in args.emulators]
for emulator in emulators:
    version = subprocess.check_output([str(emulator), '--version'], text=True, timeout=5)
    if not version.startswith('QEMU emulator version '):
        parser.error(f'{emulator} is not a system emulator')
(root / '.local').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='tcg-system-check-', dir=root / '.local') as directory:
    path = Path(directory)
    linker = path / 'layout.ld'
    linker.write_text('ENTRY(_start)\nSECTIONS { . = 0x40080000; '
                      '.text : { *(.text.start) *(.text*) } '
                      '.rodata : { *(.rodata*) } .data : { *(.data*) } '
                      '.bss : { *(.bss*) *(COMMON) } }\n')
    binary = path / 'check.elf'
    subprocess.run(['clang', '--target=aarch64-none-elf', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-fno-stack-protector', '-mgeneral-regs-only', '-nostdlib',
                    '-static', '-fuse-ld=lld', str(root / 'tools/tcg-system-check.c'),
                    f'-Wl,-T,{linker}', '-o', str(binary)], check=True)
    for emulator in emulators:
        print(f'Diskless system-mode check: {emulator}', flush=True)
        result = subprocess.run([str(emulator), '-machine', 'virt,virtualization=off',
                                 '-cpu', 'cortex-a76', '-accel', 'tcg', '-smp', '1', '-m', '128M',
                                 '-nodefaults', '-display', 'none', '-serial', 'none', '-monitor', 'none',
                                 '-semihosting-config', 'enable=on,target=native',
                                 '-kernel', str(binary)], capture_output=True, text=True, timeout=30)
        print(result.stdout + result.stderr, end='')
        if result.returncode or 'TCG system check PASSED' not in result.stdout + result.stderr:
            raise RuntimeError(f'System-mode check failed: {emulator} (exit {result.returncode})')
