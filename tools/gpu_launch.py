"""Translate an explicit host readback switch into matching guest boot settings."""
import os
from pathlib import Path
import sys


def boot_arguments(arguments, mode):
    if mode is None:
        return arguments
    if mode not in ('0', '1'):
        raise ValueError('IOS_KVM_ASYNC_READBACK must be 0 or 1 for the launcher; use guest marker mode only for development comparisons')
    # Both halves must agree. Replace earlier manual settings to avoid XNU's
    # first-match boot-argument parsing disagreeing with the EAGL environment.
    kept = [word for word in arguments.split()
            if not word.startswith(('gpu_async=', 'glenv=IOS_KVM_ASYNC_READBACK='))]
    return ' '.join(kept + [f'gpu_async={mode}', f'glenv=IOS_KVM_ASYNC_READBACK={mode}'])


def requested_mode(environment, root):
    if 'IOS_KVM_ASYNC_READBACK' in environment:
        return environment['IOS_KVM_ASYNC_READBACK']
    path = root / '.local/gpu-async'
    try:
        value = path.read_text().strip()
    except FileNotFoundError:
        return None
    if value not in ('0', '1'):
        raise ValueError(f'{path} must contain 0 or 1')
    return value


def main():
    try:
        print(boot_arguments(os.environ.get('CMDLINE', ''), requested_mode(os.environ, Path(__file__).resolve().parents[1])))
        return 0
    except (ValueError, OSError) as error:
        print(f'ios-kvm: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
