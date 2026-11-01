#!/usr/bin/env python3
"""Build and validate native GLES against the existing guest, without restarting it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--profile', required=True, type=Path, help='Existing gpu_host.py profile')
p.add_argument('--output', required=True, type=Path, help='New directory for test receipts')
p.add_argument('--port', type=int, default=6733)
p.add_argument('--server', type=Path, help='Use a separate instrumented native server binary')
a = p.parse_args()
if not 1024 <= a.port <= 65535:
    p.error('port must be between 1024 and 65535')
profile = json.loads(a.profile.read_text())
selected = profile.get('selected') or {}
environment = selected.get('environment', {})
if not selected or not set(environment) <= {'LIBGL_ALWAYS_SOFTWARE', '__EGL_VENDOR_LIBRARY_FILENAMES', 'VTEST_RENDERNODE'} or not all(isinstance(v, str) for v in environment.values()):
    p.error('profile lacks a valid selected renderer')
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=False)
with socket.socket() as probe:
    probe.bind(('127.0.0.1', a.port))
subprocess.run(['python3', str(root / 'tools/build-native-gl.py')], cwd=root, check=True)
local = root / '.local/native-gl-api'
server_binary = a.server.resolve() if a.server else local / 'server'
ssh = ['bash', str(root / 'tools/ssh.sh'), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10']
server = None
guest_path = None
receipt = {'scope': 'Native GLES API correctness, not desktop integration or performance',
           'profile': str(a.profile.resolve()), 'selected': selected, 'port': a.port}
error = None
with tempfile.TemporaryDirectory(prefix='native-gl-check-', dir=root / '.local') as directory, \
        (a.output / 'server.log').open('w') as server_log, \
        (a.output / 'deploy.log').open('w') as deploy_log:
    temporary = Path(directory)
    token = temporary / 'token'
    fd = os.open(token, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        os.write(fd, os.urandom(32))
    finally:
        os.close(fd)
    try:
        server = subprocess.Popen([str(server_binary), str(token), str(a.port)],
                                  env={**os.environ, **environment}, stdout=server_log,
                                  stderr=subprocess.STDOUT, cwd=root)
        deadline = time.monotonic() + 10
        while True:
            if server.poll() is not None:
                raise RuntimeError('native server exited; inspect server.log')
            try:
                with socket.create_connection(('127.0.0.1', a.port), timeout=.2):
                    break
            except OSError:
                if time.monotonic() > deadline:
                    raise RuntimeError('native server did not listen')
                time.sleep(.1)
        with (a.output / 'protocol.log').open('w') as log:
            subprocess.run(['python3', str(root / 'tools/native-gl/test-protocol.py'),
                            '--generated', str(local), '--token', str(token), '--port', str(a.port)],
                           cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=45, check=True)
        guest_path = '/var/tmp/ios-kvm-native-api-' + temporary.name
        command = f'mkdir -p {shlex.quote(guest_path)} && chmod 700 {shlex.quote(guest_path)}'
        subprocess.run([*ssh, command], cwd=root, stdout=deploy_log, stderr=deploy_log, timeout=30, check=True)
        for source, name, mode in [(local / 'libios-kvm-native-gl.dylib', 'libios-kvm-native-gl.dylib', '600'),
                                   (local / 'guest-check', 'guest-check', '700'), (token, 'token', '600')]:
            path = shlex.quote(guest_path + '/' + name)
            command = f'umask 077; cat > {path} && chmod {mode} {path}'
            subprocess.run([*ssh, command], input=source.read_bytes(), cwd=root,
                           stdout=deploy_log, stderr=deploy_log, timeout=30, check=True)
        command = (f'export IOS_KVM_NATIVE_GL_HOST=10.0.2.2 IOS_KVM_NATIVE_GL_PORT={a.port} '
                   f'IOS_KVM_NATIVE_GL_TOKEN_FILE={shlex.quote(guest_path + "/token")}; '
                   f'{shlex.quote(guest_path + "/guest-check")}')
        result = subprocess.run([*ssh, command], cwd=root, capture_output=True, text=True, timeout=45)
        output = result.stdout + result.stderr
        (a.output / 'guest.log').write_text(output)
        if result.returncode or 'Native GL general API check PASSED' not in output:
            raise RuntimeError('guest correctness check failed; inspect guest.log')
        renderer = next(line.split(': ', 1)[1] for line in output.splitlines() if line.startswith('Native proxy renderer: '))
        software = any(s in renderer.lower() for s in ('llvmpipe', 'softpipe', 'swrast'))
        if renderer.strip() != selected.get('renderer', '').strip():
            raise RuntimeError('actual renderer differs from selected profile')
        if software == bool(selected.get('hardware')):
            raise RuntimeError('actual hardware/software renderer disagrees with profile')
        receipt.update(passed=True, actual_renderer=renderer,
                       hashes={name: hashlib.sha256((local / name).read_bytes()).hexdigest()
                               for name in ['libios-kvm-native-gl.dylib', 'guest-check']})
        receipt['hashes']['server'] = hashlib.sha256(server_binary.read_bytes()).hexdigest()
    except Exception as exc:
        receipt.update(passed=False, error=str(exc))
        error = exc
    finally:
        if server and server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=5)
        if guest_path:
            command = 'rm -rf -- ' + shlex.quote(guest_path)
            try:
                r = subprocess.run([*ssh, command], cwd=root, stdout=deploy_log, stderr=deploy_log, timeout=20)
                receipt['guest_temp_removed'] = r.returncode == 0
            except subprocess.TimeoutExpired:
                receipt['guest_temp_removed'] = False
        receipt['server_stopped'] = server is None or server.poll() is not None
(a.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
if error:
    raise SystemExit(f'Native GL check failed: {error}; receipts in {a.output}')
print(f'Native GL check passed on {receipt["actual_renderer"]}; receipts in {a.output}')
