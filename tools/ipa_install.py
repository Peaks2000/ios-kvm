"""Install a local IPA through the running guest's TrollStore Lite root helper."""
import argparse
from pathlib import Path
import plistlib
import subprocess
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[1]
HELPER = '/var/jb/Applications/TrollStoreLite.app/trollstorehelper'


class InstallError(Exception):
    pass


def inspect_ipa(path):
    path = Path(path).expanduser().resolve()
    if not path.is_file() or path.suffix.lower() != '.ipa':
        raise InstallError('Choose a local .ipa file.')
    try:
        with zipfile.ZipFile(path) as archive:
            candidates = [i for i in archive.infolist() if i.filename.startswith('Payload/')
                          and i.filename.count('/') == 2 and i.filename.endswith('.app/Info.plist')]
            if len(candidates) != 1 or candidates[0].file_size > 1024 * 1024:
                raise InstallError('The IPA must contain one app with a valid Info.plist.')
            with archive.open(candidates[0]) as source:
                data = source.read(1024 * 1024 + 1)
            if len(data) > 1024 * 1024:
                raise InstallError('The app Info.plist is too large.')
            info = plistlib.loads(data)
    except (OSError, zipfile.BadZipFile, RuntimeError, ValueError, plistlib.InvalidFileException) as error:
        raise InstallError(f'Cannot read IPA: {error}') from error
    identifier = info.get('CFBundleIdentifier') if isinstance(info, dict) else None
    if not isinstance(identifier, str) or not identifier.strip():
        raise InstallError('The app has no bundle identifier.')
    name = info.get('CFBundleDisplayName') or info.get('CFBundleName') or identifier
    return path, str(name), identifier


def install(path, status=print):
    path, name, identifier = inspect_ipa(path)
    ssh = ['bash', str(ROOT / 'tools/ssh.sh'), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8',
           '-o', 'ServerAliveInterval=5', '-o', 'ServerAliveCountMax=3']
    remote = '/var/tmp/ios-kvm-install-' + uuid.uuid4().hex + '.ipa'
    cleanup = False
    try:
        status('Connecting to the VM…')
        probe = subprocess.run(ssh + [f'test -x {HELPER}'], capture_output=True, text=True, timeout=20)
        if probe.returncode:
            raise InstallError('VM unavailable or TrollStore Lite is missing. Start the VM and install TrollStore Lite first.')
        status(f'Uploading {path.name} ({path.stat().st_size / 1048576:.1f} MiB)…')
        cleanup = True
        with path.open('rb') as source:
            upload = subprocess.run(ssh + [f'umask 077; cat > {remote}'], stdin=source,
                                    capture_output=True, text=True, timeout=600)
        if upload.returncode:
            raise InstallError('IPA upload failed: ' + upload.stderr[-1000:])
        status(f'Installing {name}…')
        result = subprocess.run(ssh + [f'{HELPER} install {remote}'],
                                capture_output=True, text=True, timeout=600)
        if result.returncode not in (0, 182, 184):
            log = ROOT / '.local/ipa-install-last.log'
            log.parent.mkdir(parents=True, exist_ok=True)
            log.write_text(result.stdout + result.stderr)
            log.chmod(0o600)
            raise InstallError(f'Install failed (code {result.returncode}). Details: {log}')
        suffix = {182: ' iOS developer mode needs attention before launching it.',
                  184: ' Some embedded binaries are encrypted and may not run.'}.get(result.returncode, '')
        status(f'Installed {name}. Find it on the iOS home screen or in App Library.' + suffix)
        return identifier
    except subprocess.TimeoutExpired as error:
        raise InstallError('The VM did not finish responding. Check that it has completed startup.') from error
    except OSError as error:
        raise InstallError(str(error)) from error
    finally:
        if cleanup:
            try:
                subprocess.run(ssh + [f'/var/jb/usr/bin/rm -f {remote}'],
                               capture_output=True, timeout=15)
            except (OSError, subprocess.TimeoutExpired):
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ipa', type=Path)
    args = parser.parse_args()
    try:
        install(args.ipa)
    except InstallError as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
