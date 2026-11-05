#!/usr/bin/env python3
"""Temporarily try native GLES in backboardd, then restore its saved injection.

This is an explicit research experiment, not a release-readiness gate or default
launcher. A separate guest watchdog restores the original configuration too.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import socket
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--profile', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--port', type=int, default=6777)
p.add_argument('--seconds', type=int, default=60, choices=range(15, 301), metavar='15..300')
p.add_argument('--async-readback', action='store_true')
a = p.parse_args()
if not 1024 <= a.port <= 65535:
    p.error('invalid port')
selected = json.loads(a.profile.read_text())['selected']
environment = selected['environment']
if not set(environment) <= {'LIBGL_ALWAYS_SOFTWARE', '__EGL_VENDOR_LIBRARY_FILENAMES', 'VTEST_RENDERNODE'}:
    p.error('invalid renderer environment')
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=False, mode=0o700)
subprocess.run(['python3', str(root / 'tools/build-native-gl.py')], cwd=root, check=True)
subprocess.run(['python3', str(root / 'tools/native-gl/eagl-coverage.py')], cwd=root, check=True)
local = root / '.local/native-gl-api'
bundle=a.output/'bundle'
bundle.mkdir(mode=0o700)
server_binary=bundle/'server'
proxy=bundle/'libios-kvm-native-gl.dylib'
bridge=bundle/'libhakios-native-eagl.dylib'
for source,destination in [(local/'server',server_binary),
                           (local/'libios-kvm-native-gl.dylib',proxy),
                           (local/'eagl-coverage/libhakios-native-eagl.dylib',bridge)]:
    shutil.copy2(source,destination)
spec = importlib.util.spec_from_file_location('compositor_qmp', root / 'vendor/hakios/qemu/touch.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def qmp_call(callback):
    q = module.Qmp()
    q.sock.settimeout(10)
    try:
        return callback(q)
    finally:
        q.file.close()
        q.sock.close()

def ram_info(q):
    if q.command('query-name').get('name') != 'ios-kvm' or not q.command('query-status').get('running'):
        raise RuntimeError('Expected a running ios-kvm VM')
    if not q.command('qom-get', path='/objects/ram', property='share'):
        raise RuntimeError('VM RAM is not shared')
    return {'IOS_KVM_RAM_FILE': q.command('qom-get', path='/objects/ram', property='mem-path'),
            'IOS_KVM_RAM_SIZE': str(q.command('qom-get', path='/objects/ram', property='size'))}

ram_environment = qmp_call(ram_info)
session = json.loads((root / '.local/vm-session.json').read_text())
vm_log = Path(session['logs']) / 'vm.log'
ssh = ['bash', str(root / 'tools/ssh.sh'), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8']

def remote(command, **kwargs):
    return subprocess.run([*ssh, command], cwd=root, timeout=20, **kwargs)

def screenshot(name):
    path = a.output / (name + '.ppm')
    qmp_call(lambda q: q.command('screendump', filename=str(path)))
    from PIL import Image
    with Image.open(path) as picture:
        picture.save(a.output / (name + '.png'))
    path.unlink()

def config_read():
    return remote('/var/jb/usr/sbin/sysctl -n kern.exec_inject', capture_output=True,
                  text=True, check=True).stdout.strip()

def config_write(config, log):
    command = '/var/jb/usr/sbin/sysctl -w ' + shlex.quote('kern.exec_inject=' + config)
    remote(command, stdout=log, stderr=log, check=True)

receipt = {'scope': 'Temporary native compositor trial; not desktop compatibility or performance proof',
           'selected': selected, 'seconds_requested': a.seconds, 'original_restored': False,
           'async_readback_requested': a.async_readback}
server = None
guest = None
original = None
changed = False
watchdog_pid = None
failure = None
with tempfile.TemporaryDirectory(prefix='native-desktop-', dir=root / '.local') as directory, \
        (a.output / 'server.log').open('w+') as server_log, \
        (a.output / 'deploy.log').open('w') as deploy_log:
    temporary = Path(directory)
    guest = '/var/tmp/ios-kvm-' + temporary.name
    mobile = guest + '/mobile'
    token = temporary / 'token'
    token.write_bytes(os.urandom(32))
    token.chmod(0o600)
    injection = a.output / 'libios-kvm-native-inject.dylib'
    subprocess.run(['clang', '-Qunused-arguments', '-target', 'arm64-apple-ios16.0',
                    '-isysroot', str(root / '.local/sdks/iPhoneOS16.5.sdk'), '-fuse-ld=lld',
                    '-O2', '-dynamiclib', '-DIOS_KVM_NATIVE_GL=1',
                    '-DHAKIOS_GL=' + json.dumps(mobile + '/' + bridge.name),
                    '-DIOS_KVM_NATIVE_TOKEN_FILE=' + json.dumps(mobile + '/token'),
                    '-DIOS_KVM_NATIVE_PORT=' + json.dumps(str(a.port)),
                    '-DIOS_KVM_NATIVE_ASYNC_MODE=' + json.dumps(str(int(a.async_readback))),
                    '-install_name', mobile + '/' + injection.name,
                    '-framework', 'CoreFoundation', '-framework', 'IOKit', '-lobjc',
                    str(root / 'vendor/hakios/userland/src/inject.c'),
                    str(root / 'tools/native-gl/bootstrap.c'), '-o', str(injection)], check=True)
    try:
        original = config_read()
        if 'native-desktop-' in original:
            raise RuntimeError('A previous native experiment is still selected; restore it first')
        saved = a.output / 'exec-inject-before.txt'
        saved.write_text(original + '\n')
        saved.chmod(0o600)
        screenshot('before')
        start_offset = vm_log.stat().st_size
        server = subprocess.Popen([str(server_binary), str(token), str(a.port)], cwd=root,
                                  env={**os.environ, **environment, **ram_environment, 'IOS_KVM_NATIVE_TRACE_ERRORS':'1'},
                                  stdout=server_log, stderr=server_log)
        deadline = time.monotonic() + 10
        while True:
            if server.poll() is not None:
                raise RuntimeError('Native server exited before listening')
            try:
                with socket.create_connection(('127.0.0.1', a.port), timeout=.2):
                    break
            except OSError:
                if time.monotonic() > deadline:
                    raise RuntimeError('Native server did not listen')
                time.sleep(.1)
        remote(f'mkdir -m 711 {shlex.quote(guest)} && mkdir -m 700 {shlex.quote(mobile)}',
               stdout=deploy_log, stderr=deploy_log, check=True)
        watchdog = (f'#!/var/jb/usr/bin/sh\nset -eu\n/var/jb/usr/bin/sleep {a.seconds + 60}\n'
                    f'[ ! -e {shlex.quote(guest + "/finished")} ] || exit 0\n'
                    f'cfg=$(/var/jb/usr/bin/cat {shlex.quote(guest + "/original.cfg")})\n'
                    '/var/jb/usr/sbin/sysctl -w "kern.exec_inject=$cfg" >/dev/null\n'
                    '/var/jb/usr/bin/killall -TERM backboardd\n'
                    'echo "Native compositor watchdog restored original injection"\n')
        files = [(injection.read_bytes(), mobile + '/' + injection.name, '600'),
                 (bridge.read_bytes(), mobile + '/' + bridge.name, '600'),
                 (proxy.read_bytes(), mobile + '/libios-kvm-native-gl.dylib', '600'),
                 (token.read_bytes(), mobile + '/token', '600'),
                 ((original + '\n').encode(), guest + '/original.cfg', '600'),
                 (watchdog.encode(), guest + '/watchdog.sh', '700')]
        for data, target, mode in files:
            path = shlex.quote(target)
            remote(f'umask 077; cat > {path} && chmod {mode} {path}', input=data,
                   stdout=deploy_log, stderr=deploy_log, check=True)
        remote('/var/jb/usr/bin/chown -R 501:501 ' + shlex.quote(mobile),
               stdout=deploy_log, stderr=deploy_log, check=True)
        result = remote(f'/var/jb/usr/bin/nohup /var/jb/usr/bin/sh {shlex.quote(guest + "/watchdog.sh")} '
                        f'> {shlex.quote(guest + "/watchdog.log")} 2>&1 < /dev/null & echo $!',
                        capture_output=True, text=True, check=True)
        watchdog_pid = int(result.stdout.strip())
        remote(f'kill -0 {watchdog_pid}', stdout=deploy_log, stderr=deploy_log, check=True)
        receipt['watchdog_pid'] = watchdog_pid
        entries = []
        for entry in original.split(';'):
            parts = entry.split()
            if not parts:
                continue
            library, *names = parts
            names = [name for name in names if name != 'backboardd']
            if names:
                entries.append(' '.join([library, *names]))
        entries.append(mobile + '/' + injection.name + ' backboardd')
        candidate = ';'.join(entries)
        if qmp_call(ram_info) != ram_environment:
            raise RuntimeError('VM RAM changed before experiment')
        changed = True  # Restore even if the write's acknowledgement is lost.
        config_write(candidate, deploy_log)
        remote('/var/jb/usr/bin/killall -TERM backboardd', stdout=deploy_log, stderr=deploy_log, check=True)
        print('Temporary native compositor selected; timed rollback armed', flush=True)
        started = time.monotonic()
        captured = False
        while time.monotonic() - started < a.seconds:
            if server.poll() is not None:
                raise RuntimeError('Native host exited during compositor trial')
            with vm_log.open('r', errors='replace') as log:
                log.seek(start_offset)
                output = log.read()
            (a.output / 'vm-native.log').write_text(output)
            server_log.flush()
            server_log.seek(0)
            host_output = server_log.read()
            for marker in ('ios-kvm native GL:', 'native shared readback failed', 'CALLED AND NOT THERE', 'A SHADER DOES NOT COMPILE', 'A PROGRAM DOES NOT LINK'):
                if marker in output:
                    receipt['failure_marker'] = marker
                    raise RuntimeError('Compositor reached unsupported/error path: ' + marker)
            if 'native GLES: rejected' in host_output:
                raise RuntimeError('Native host rejected a compositor request')
            if 'native GLES driver error' in host_output or 'native GLES recorded error' in host_output:
                raise RuntimeError('Native driver reported a compositor GL error')
            if not captured and time.monotonic() - started > 20:
                screenshot('during')
                captured = True
            time.sleep(.5)
        screenshot('during-final')
        receipt.update(observation_finished=True,
                       native_injection_seen='temporary native compositor experiment UID501' in output,
                       native_context_seen='native GLES client:' in host_output,
                       async_submission_seen='native GLES asynchronous retained submission active' in output,
                       actual_host_renderers=sorted(set(line.split(': ', 1)[1] for line in host_output.splitlines()
                                                        if line.startswith('native GLES client: '))))
        if not receipt['native_injection_seen'] or not receipt['native_context_seen']:
            raise RuntimeError('Native injection/context did not become active during observation')
        if a.async_readback and not receipt['async_submission_seen']:
            raise RuntimeError('Asynchronous retained submissions did not become active')
        if receipt['actual_host_renderers'] != [selected.get('renderer')]:
            raise RuntimeError('Native compositor renderer differs from selected profile')
    except Exception as exc:
        failure = str(exc)
        receipt['error'] = failure
    finally:
        if changed and original is not None:
            try:
                config_write(original, deploy_log)
                receipt['original_restored'] = config_read() == original
                remote('/var/jb/usr/bin/killall -TERM backboardd', stdout=deploy_log, stderr=deploy_log)
                print('Original compositor injection restored; waiting for VirGL recovery', flush=True)
                time.sleep(8)
                screenshot('after-restore')
            except Exception as exc:
                receipt['restore_error'] = str(exc)
        safe_to_stop = not changed or receipt['original_restored']
        if safe_to_stop:
            if watchdog_pid:
                remote(f'kill -TERM {watchdog_pid}', stdout=deploy_log, stderr=deploy_log)
            if server and server.poll() is None:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=5)
            try:
                receipt['guest_temp_removed'] = remote('rm -rf -- ' + shlex.quote(guest),
                                                       stdout=deploy_log, stderr=deploy_log).returncode == 0
            except Exception as exc:
                receipt['cleanup_error'] = str(exc)
        else:
            receipt['recovery_retained'] = True
            receipt['recovery_guest_directory'] = guest
            receipt['recovery_server_pid'] = server.pid if server else None
        receipt['server_stopped'] = not server or server.poll() is not None
        receipt['hashes'] = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in
                             [('server', server_binary), ('proxy', proxy),
                              ('bridge', bridge), ('injection', injection)]}
        (a.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
if failure or not receipt['original_restored']:
    raise SystemExit(f'Native compositor trial stopped: {failure}; receipts in {a.output}')
print(f'Native compositor observation finished; restored original injection; receipts in {a.output}')
