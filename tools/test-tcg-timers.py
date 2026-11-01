#!/usr/bin/env python3
"""Check ARM counters and timer interrupts in a diskless 128 MiB EL1 guest.

Runs ordinary TCG and instruction-counted TCG. Optional record/replay exercises
QEMU's deterministic clock path. No iOS firmware, NAND, or running VM is used.
The caller's environment is preserved to test opt-in emulator changes.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--record-replay', action='store_true')
parser.add_argument('emulators', nargs='+', type=Path)
args = parser.parse_args()
emulators = [p.resolve() for p in args.emulators]
(root / '.local').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='tcg-timer-', dir=root / '.local') as directory:
    path = Path(directory)
    linker = path / 'layout.ld'
    linker.write_text('ENTRY(_start)\nSECTIONS { . = 0x40080000; '
                      '.text : { *(.text.start) *(.text*) } '
                      '.rodata : { *(.rodata*) } .data : { *(.data*) } '
                      '.bss : { *(.bss*) *(COMMON) } }\n')
    binary = path / 'check.elf'
    subprocess.run(['clang', '--target=aarch64-none-elf', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-fno-stack-protector', '-mgeneral-regs-only', '-nostdlib',
                    '-static', '-fuse-ld=lld', str(root / 'tools/tcg-timer-check.c'),
                    str(root / 'tools/tcg-timer-vectors.S'), f'-Wl,-T,{linker}',
                    '-o', str(binary)], check=True)
    for emulator in emulators:
        modes = [('ordinary', []), ('icount', ['-icount', 'shift=3'])]
        if args.record_replay:
            modes += [(mode, ['-icount', f'shift=3,rr={mode},rrfile={path}/timer.rr'])
                      for mode in ('record', 'replay')]
        for mode, extra in modes:
            print(f'Timer check ({mode}): {emulator}', flush=True)
            result = subprocess.run([str(emulator), '-machine', 'virt,virtualization=off,gic-version=2',
                                     '-cpu', 'cortex-a76', '-accel', 'tcg', '-smp', '1', '-m', '128M',
                                     '-nodefaults', '-display', 'none', '-serial', 'none', '-monitor', 'none',
                                     '-semihosting-config', 'enable=on,target=native',
                                     '-kernel', str(binary), *extra],
                                    capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            print(output, end='')
            if result.returncode or 'Counter/timer PASSED' not in output:
                raise RuntimeError(f'Timer check failed: {emulator} ({mode}, exit {result.returncode})')
