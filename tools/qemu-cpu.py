#!/usr/bin/env python3
"""Select CPU execution for the ARM guest; KVM is explicit and ARM64 Linux only."""
import argparse
import os
import platform
import stat
import subprocess
import sys


def configuration(environment, binary):
    mode = environment.get('QEMU_ACCEL', 'tcg')
    if mode == 'tcg':
        size = environment.get('QEMU_TB_MB', '384')
        if not size.isdecimal() or int(size) < 1:
            raise ValueError('QEMU_TB_MB must be a positive integer')
        return ['-cpu', 'cortex-a76', '-accel', f'tcg,tb-size={size}']
    if mode != 'kvm':
        raise ValueError('QEMU_ACCEL must be tcg or kvm')
    if platform.system() != 'Linux' or platform.machine().lower() not in ('aarch64', 'arm64'):
        raise ValueError('ARM guest KVM requires an ARM64 Linux host; use QEMU_ACCEL=tcg here')
    try:
        device = os.stat('/dev/kvm')
    except OSError as error:
        raise ValueError(f'KVM is unavailable: {error}') from error
    if not stat.S_ISCHR(device.st_mode) or not os.access('/dev/kvm', os.R_OK | os.W_OK):
        raise ValueError('KVM requires read/write access to the /dev/kvm character device')
    result = subprocess.run([binary, '-accel', 'help'], capture_output=True, text=True,
                            timeout=5, check=True)
    if 'kvm' not in {line.strip() for line in result.stdout.splitlines()}:
        raise ValueError('The selected qemu-system-aarch64 was built without KVM')
    return ['-cpu', 'host', '-accel', 'kvm']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    binary = command[0] if command else os.environ.get('QEMU_BINARY', 'qemu-system-aarch64')
    try:
        cpu = configuration(os.environ, binary)
        if args.check:
            return 0
        if not command:
            parser.error('provide the QEMU executable and its arguments after --')
        if os.environ.get('QEMU_ACCEL') == 'kvm':
            print('ios-kvm: experimental ARM64 KVM; iOS boot compatibility is not yet validated', flush=True)
        os.execvpe(binary, [binary, *cpu, *command[1:]], os.environ)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f'ios-kvm: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
