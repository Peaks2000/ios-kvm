#!/usr/bin/env python3
"""Kill a temporary guest producer during retained native GPU readback.

Uses a separate host binary with a compile-only pause hook. Never stops the VM
or compositor and never installs a runtime library.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import select
import shlex
import signal
import socket
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--profile', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--port', type=int, default=6758)
p.add_argument('--sanitize', action='store_true')
p.add_argument('--async-readback', action='store_true')
a = p.parse_args()
if not 1024 <= a.port <= 65535:
    p.error('invalid port')
selected = json.loads(a.profile.read_text())['selected']
environment = selected['environment']
if not set(environment) <= {'LIBGL_ALWAYS_SOFTWARE', '__EGL_VENDOR_LIBRARY_FILENAMES', 'VTEST_RENDERNODE'}:
    p.error('invalid renderer environment')
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=False)
subprocess.run(['python3', str(root / 'tools/build-native-gl.py')], cwd=root, check=True)
local = root / '.local/native-gl-api'
host = a.output / 'server-exit-check'
libs = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'egl', 'glesv2'], text=True))
flags = ['-O1', '-g', '-Wall', '-Wextra', '-Werror', '-DNG_SHARED_TEST_HOOKS']
if a.sanitize:
    flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
subprocess.run(['clang', *flags, '-I', str(root / 'tools/native-gl'), '-I', str(local),
                str(root / 'tools/native-gl/server.c'), '-o', str(host), *libs, '-pthread'], check=True)
spec = importlib.util.spec_from_file_location('native_exit_qmp', root / 'vendor/hakios/qemu/touch.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
q = module.Qmp()
try:
    if q.command('query-name').get('name') != 'ios-kvm' or not q.command('query-status').get('running'):
        raise RuntimeError('Expected a running ios-kvm VM')
    if not q.command('qom-get', path='/objects/ram', property='share'):
        raise RuntimeError('VM RAM is not shared')
    ram_environment = {'IOS_KVM_RAM_FILE': q.command('qom-get', path='/objects/ram', property='mem-path'),
                       'IOS_KVM_RAM_SIZE': str(q.command('qom-get', path='/objects/ram', property='size'))}
finally:
    q.file.close()
    q.sock.close()
ssh = ['bash', str(root / 'tools/ssh.sh'), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10']
server = producer = observer = None
guest = None
victim_pid = None
receipt = {'scope': 'Native retained readback across temporary guest process exit; not desktop or performance',
           'selected': selected, 'sanitized': a.sanitize, 'async_readback': a.async_readback}
failure = None
notify_read, notify_write = os.pipe()

def remote(command, **kwargs):
    return subprocess.run([*ssh, command], cwd=root, timeout=20, **kwargs)

def wait_until(predicate, process, label, seconds=20):
    deadline = time.monotonic() + seconds
    while not predicate():
        if process.poll() is not None:
            raise RuntimeError(label + ': process exited')
        if time.monotonic() >= deadline:
            raise RuntimeError(label + ': deadline exceeded')
        time.sleep(.005)

with tempfile.TemporaryDirectory(prefix='native-exit-', dir=root / '.local') as directory, \
        (a.output / 'server.log').open('w+') as server_log, \
        (a.output / 'deploy.log').open('w') as deploy_log, \
        (a.output / 'producer.log').open('w') as producer_log, \
        (a.output / 'observer.log').open('w') as observer_log:
    token = Path(directory) / 'token'
    token.write_bytes(os.urandom(32))
    token.chmod(0o600)
    try:
        server = subprocess.Popen([str(host), str(token), str(a.port)], cwd=root,
                                  env={**os.environ, **environment, **ram_environment,
                                       'IOS_KVM_NATIVE_TEST_NOTIFY_FD': str(notify_write),
                                       'ASAN_OPTIONS': 'detect_leaks=0'},
                                  pass_fds=(notify_write,), stdout=server_log, stderr=server_log)
        os.close(notify_write)
        notify_write = -1
        def listening():
            try:
                with socket.create_connection(('127.0.0.1', a.port), timeout=.1):
                    return True
            except OSError:
                return False
        wait_until(listening, server, 'server startup', 10)
        guest = '/var/tmp/ios-kvm-' + Path(directory).name
        remote('mkdir -m 700 ' + shlex.quote(guest), stdout=deploy_log, stderr=deploy_log, check=True)
        for source, name, mode in [(local / 'libios-kvm-native-gl.dylib', 'libios-kvm-native-gl.dylib', '600'),
                                   (local / 'exit-check', 'exit-check', '700'), (token, 'token', '600')]:
            path = shlex.quote(guest + '/' + name)
            remote(f'umask 077; cat > {path} && chmod {mode} {path}', input=source.read_bytes(),
                   stdout=deploy_log, stderr=deploy_log, check=True)
        env = (f'export IOS_KVM_NATIVE_GL_HOST=10.0.2.2 IOS_KVM_NATIVE_GL_PORT={a.port} '
               f'IOS_KVM_NATIVE_GL_TOKEN_FILE={shlex.quote(guest + "/token")} IOS_KVM_NATIVE_SHARED=1 '
               f'IOS_KVM_NATIVE_ASYNC_READBACK={int(a.async_readback)}; ')
        producer = subprocess.Popen([*ssh, env + 'exec ' + shlex.quote(guest + '/exit-check')],
                                    cwd=root, stdout=producer_log, stderr=producer_log)
        if not select.select([notify_read], [], [], 30)[0] or os.read(notify_read, 1) != b'R':
            raise RuntimeError('Host never accepted a retained readback')
        status_path = Path(f'/proc/{server.pid}/status')
        wait_until(lambda: re.search(r'^State:\s+T', status_path.read_text(), re.M),
                   server, 'host pause')
        if a.async_readback:
            wait_until(lambda: 'submission returned before host completion' in
                       (a.output / 'producer.log').read_text(), producer,
                       'nonblocking retained submission')
            receipt['submission_returned_while_host_paused'] = True
        match = re.search(r'Native exit producer: pid=(\d+) surface=(\d+)',
                          (a.output / 'producer.log').read_text())
        if not match:
            raise RuntimeError('Producer PID/surface record missing')
        victim_pid, sid = map(int, match.groups())
        remote(f'kill -KILL {victim_pid}', stdout=deploy_log, stderr=deploy_log, check=True)
        producer.wait(timeout=10)
        remote(f'if kill -0 {victim_pid} 2>/dev/null; then exit 1; fi',
               stdout=deploy_log, stderr=deploy_log, check=True)
        receipt.update(producer_pid=victim_pid, surface_id=sid, producer_confirmed_dead=True)
        observer = subprocess.Popen([*ssh, 'exec ' + shlex.quote(guest + '/exit-check') + f' --observe {sid}'],
                                    cwd=root, stdout=observer_log, stderr=observer_log)
        wait_until(lambda: 'pending surface found after producer exit' in (a.output / 'observer.log').read_text(),
                   observer, 'observer pending fence')
        receipt['observer_found_pending_surface'] = True
        server.send_signal(signal.SIGCONT)
        if observer.wait(timeout=20) != 0:
            raise RuntimeError('Observer pixel/lifetime checks failed')
        if 'Native shared process-exit check PASSED' not in (a.output / 'observer.log').read_text():
            raise RuntimeError('Observer did not finish')
        if server.poll() is not None:
            raise RuntimeError('Host did not survive producer disconnect')
        server_log.flush()
        server_log.seek(0)
        log = server_log.read()
        if any(s in log for s in ('ERROR: AddressSanitizer', 'runtime error:', 'UndefinedBehaviorSanitizer')):
            raise RuntimeError('Host sanitizer reported an error')
        receipt.update(passed=True, hashes={name: hashlib.sha256((local / name).read_bytes()).hexdigest()
                                           for name in ('libios-kvm-native-gl.dylib', 'exit-check')})
        receipt['hashes']['host'] = hashlib.sha256(host.read_bytes()).hexdigest()
    except Exception as exc:
        failure = str(exc)
        receipt.update(passed=False, error=failure)
    finally:
        if server and server.poll() is None:
            server.send_signal(signal.SIGCONT)
        if victim_pid and producer and producer.poll() is None:
            remote(f'kill -KILL {victim_pid}', stdout=deploy_log, stderr=deploy_log)
        for process in (producer, observer, server):
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        if guest:
            try:
                receipt['guest_temp_removed'] = remote('rm -rf -- ' + shlex.quote(guest),
                                                       stdout=deploy_log, stderr=deploy_log).returncode == 0
            except subprocess.TimeoutExpired:
                receipt['guest_temp_removed'] = False
        receipt['server_stopped'] = not server or server.poll() is not None
os.close(notify_read)
if notify_write >= 0:
    os.close(notify_write)
(a.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
if failure:
    raise SystemExit(f'Native exit check failed: {failure}; inspect {a.output}')
print(f'Native shared process-exit check passed; receipts in {a.output}')
