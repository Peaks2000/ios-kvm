#!/usr/bin/env python3
"""Update project sources without formatting or replacing device storage."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]


def git(*args, check=True):
    return subprocess.run(['git', *args], cwd=ROOT, check=check,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def output(*args):
    return git(*args).stdout.decode().strip()


def protected(path):
    return (path in ('.local', 'firmware', 'vendor/hakios/ssh', 'vendor/hakios/userland/root')
            or path.startswith(('.local/', 'firmware/', 'vendor/hakios/ssh/',
                             'vendor/hakios/userland/root/'))
            or path == 'vendor/hakios/xnuloader/identity.h'
            or path.endswith(('.apfs', '.img', '/vars.fd'))
            or path == 'vendor/hakios/sysimage/firmware.json')


def update(source, branch, check=False):
    local = ROOT / '.local'
    local.mkdir(exist_ok=True)
    with (local / 'vm-session.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('Stop the ios-kvm VM before updating; no files were changed.')
        # Also catch manually launched QEMU processes using this checkout.
        for proc in Path('/proc').glob('[0-9]*'):
            try:
                command = (proc / 'cmdline').read_bytes().split(b'\0')
                if command and b'qemu-system' in command[0] and (proc / 'cwd').resolve().is_relative_to(ROOT):
                    raise RuntimeError('A QEMU process is using this checkout. Stop it before updating.')
            except (OSError, PermissionError):
                pass
        name = output('symbolic-ref', '--quiet', '--short', 'HEAD')
        old = output('rev-parse', 'HEAD')
        for marker in ('rebase-merge', 'rebase-apply', 'MERGE_HEAD', 'CHERRY_PICK_HEAD', 'REVERT_HEAD'):
            if Path(output('rev-parse', '--git-path', marker)).is_absolute():
                marker_path = Path(output('rev-parse', '--git-path', marker))
            else:
                marker_path = ROOT / output('rev-parse', '--git-path', marker)
            if marker_path.exists():
                raise RuntimeError('Finish the existing Git operation before updating.')
        git('check-ref-format', 'refs/heads/' + branch)
        git('fetch', '--no-tags', '--', source, 'refs/heads/' + branch)
        target = output('rev-parse', 'FETCH_HEAD')
        if git('merge-base', old, target, check=False).returncode:
            raise RuntimeError('These histories have no common ancestor. Automatic migration refused; device data is unchanged.')
        incoming = output('ls-tree', '-r', '--name-only', target).splitlines()
        tracked = set(output('ls-files').splitlines())
        for path in incoming:
            if protected(path):
                raise RuntimeError('Incoming repository tracks private device data: ' + path)
            parts = Path(path).parts
            for count in range(1, len(parts) + 1):
                candidate = ROOT.joinpath(*parts[:count])
                relative = candidate.relative_to(ROOT).as_posix()
                if (candidate.is_symlink() or (candidate.exists() and not candidate.is_dir())) and relative not in tracked:
                    raise RuntimeError('Incoming files collide with an untracked local file: ' + str(candidate))
        print(f'{name}: {old[:12]} → upstream {target[:12]}', flush=True)
        if check:
            print('Check only: source and device files are unchanged.')
            return
        backup = local / 'updates' / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + str(os.getpid()))
        backup.mkdir(parents=True, mode=0o700)
        backup.chmod(0o700)
        git('bundle', 'create', str(backup / 'history.bundle'), '--all')
        (backup / 'unstaged.patch').write_bytes(git('diff', '--binary').stdout)
        (backup / 'staged.patch').write_bytes(git('diff', '--cached', '--binary').stdout)
        metadata = {'branch': name, 'original_head': old, 'target': target,
                    'source': source, 'status': 'backed-up'}
        receipt = backup / 'receipt.json'
        def save():
            receipt.write_text(json.dumps(metadata, indent=2) + '\n')
        save()
        dirty = bool(output('status', '--porcelain', '--untracked-files=no'))
        stash = None
        if dirty:
            git('stash', 'push', '-m', 'ios-kvm-update-' + backup.name)
            stash = output('rev-parse', 'refs/stash')
            metadata['stash'] = stash
            save()
        print('Recovery backup: ' + str(backup), flush=True)
        result = git('-c', 'core.hooksPath=/dev/null', 'rebase', '--rebase-merges', target, check=False)
        if result.returncode:
            aborted = git('rebase', '--abort', check=False)
            metadata['status'] = 'rebase-failed' if aborted.returncode == 0 else 'abort-needs-attention'
            if stash and aborted.returncode == 0:
                restored = git('stash', 'apply', '--index', stash, check=False)
                metadata['local_edits_restored'] = restored.returncode == 0
            save()
            raise RuntimeError('Rebase failed; ' + metadata['status'] + '. Recovery: ' + str(backup) + '\n' + result.stderr.decode())
        if stash:
            restored = git('stash', 'apply', '--index', stash, check=False)
            if restored.returncode:
                metadata['status'] = 'local-edit-conflict'
                save()
                raise RuntimeError('Source updated, but local edits conflict. They remain in the saved stash and patches. Resolve before running the VM. Recovery: ' + str(backup))
        metadata['status'] = 'complete'
        metadata['updated_head'] = output('rev-parse', 'HEAD')
        save()
        print('Source update complete. Local edits restored; recovery stash retained if created.')
        print('Device disks, firmware, SSH keys and installed apps were not migrated or rebuilt.')
        print('See README: Updating safely for runtime rebuild instructions and limitations.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', default='https://github.com/Peaks2000/ios-kvm.git')
    parser.add_argument('--branch', default='master')
    parser.add_argument('--check', action='store_true', help='fetch and inspect without changing checkout')
    args = parser.parse_args()
    os.umask(0o077)
    try:
        update(args.source, args.branch, args.check)
    except (RuntimeError, subprocess.CalledProcessError, OSError) as error:
        detail = error.stderr.decode(errors='replace') if isinstance(error, subprocess.CalledProcessError) else str(error)
        parser.exit(1, 'Update stopped: ' + detail + '\n')


if __name__ == '__main__':
    main()
