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
p.add_argument('--eagl', action='store_true', help='Also run the isolated EAGL macro ABI diagnostic')
p.add_argument('--eagl-check-every-call', action='store_true', help='Enable native EAGL per-call error diagnostics and check error preservation')
p.add_argument('--guest-uid', type=int, choices=(0, 501), default=0, help='Run temporary diagnostics as root or the compositor mobile identity')
p.add_argument('--shared-ram', action='store_true', help='Opt into retained IOSurface readback through this VM RAM')
p.add_argument('--async-readback', action='store_true', help='Submit retained surface writes without waiting for their RPC reply')
p.add_argument('--benchmark-async-readback', action='store_true', help='Compare synchronous/asynchronous retained writes including CPU completion')
p.add_argument('--benchmark', action='store_true', help='Measure copied/shared readback ABBA at two phone resolutions')
p.add_argument('--benchmark-pixel-state', action='store_true', help='Benchmark shared reads with state queries versus context shadowing')
p.add_argument('--benchmark-vertex-state', action='store_true', help='Compare driver and cached enabled-attribute queries')
a = p.parse_args()
if a.eagl_check_every_call:
    a.eagl = True
if a.benchmark_pixel_state:
    a.benchmark = True
if a.benchmark_async_readback:
    a.benchmark = True
if a.benchmark_async_readback and a.benchmark_pixel_state:
    p.error('choose one readback benchmark mode')
if a.async_readback and not a.shared_ram:
    p.error('--async-readback requires --shared-ram')
if a.benchmark and not a.shared_ram:
    p.error('--benchmark requires --shared-ram')
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
if a.eagl:
    subprocess.run(['python3', str(root / 'tools/native-gl/build-eagl-check.py')], cwd=root, check=True)
local = root / '.local/native-gl-api'
server_binary = a.server.resolve() if a.server else local / 'server'
shared_environment = {}
if a.shared_ram:
    import importlib.util
    spec = importlib.util.spec_from_file_location('native_check_qmp', root / 'vendor/hakios/qemu/touch.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    q = module.Qmp()
    try:
        if q.command('query-name').get('name') != 'ios-kvm' or not q.command('query-status').get('running'):
            raise RuntimeError('Expected a running ios-kvm VM')
        if not q.command('qom-get', path='/objects/ram', property='share'):
            raise RuntimeError('VM RAM is not shared')
        shared_environment = {'IOS_KVM_RAM_FILE': q.command('qom-get', path='/objects/ram', property='mem-path'),
                              'IOS_KVM_RAM_SIZE': str(q.command('qom-get', path='/objects/ram', property='size'))}
    finally:
        q.file.close()
        q.sock.close()

ssh = ['bash', str(root / 'tools/ssh.sh'), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10']
server = None
guest_path = None
deployment_root = None
receipt = {'scope': 'Native GLES API correctness and optional readback benchmark; not desktop integration or FPS',
           'profile': str(a.profile.resolve()), 'selected': selected, 'port': a.port,
           'shared_ram_requested': a.shared_ram, 'benchmark_requested': a.benchmark,
           'pixel_state_benchmark_requested': a.benchmark_pixel_state,
           'eagl_per_call_checks_requested': a.eagl_check_every_call, 'guest_uid_requested': a.guest_uid}
receipt['vertex_state_benchmark_requested'] = a.benchmark_vertex_state
receipt['async_readback_requested'] = a.async_readback
receipt['async_readback_benchmark_requested'] = a.benchmark_async_readback
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
                                  env={**os.environ, **environment, **shared_environment}, stdout=server_log,
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
        deployment_root = '/var/tmp/ios-kvm-native-api-' + temporary.name
        guest_path = deployment_root + '/mobile' if a.guest_uid == 501 else deployment_root
        command = f'mkdir -m 700 {shlex.quote(deployment_root)}'
        if a.guest_uid == 501:
            command += f' && chmod 711 {shlex.quote(deployment_root)} && mkdir -m 700 {shlex.quote(guest_path)}'
        subprocess.run([*ssh, command], cwd=root, stdout=deploy_log, stderr=deploy_log, timeout=30, check=True)
        deploy = [(local / 'libios-kvm-native-gl.dylib', 'libios-kvm-native-gl.dylib', '600'),
                                   (local / 'guest-check', 'guest-check', '700'),
                                   (local / 'context-race-check', 'context-race-check', '700'),
                                   (token, 'token', '600')]
        if a.eagl:
            deploy += [(local / 'eagl-check', 'eagl-check', '700'),
                       (local / 'eagl-coverage/libhakios-native-eagl.dylib', 'libhakios-native-eagl.dylib', '600')]
        if a.guest_uid == 501:
            deploy += [(local / 'guest-exec', 'guest-exec', '700')]
        for source, name, mode in deploy:
            # The identity launcher stays root-owned in a root-owned directory.
            # Only its diagnostic child tree is writable by the target user.
            destination = deployment_root if name == 'guest-exec' else guest_path
            path = shlex.quote(destination + '/' + name)
            command = f'umask 077; cat > {path} && chmod {mode} {path}'
            subprocess.run([*ssh, command], input=source.read_bytes(), cwd=root,
                           stdout=deploy_log, stderr=deploy_log, timeout=30, check=True)
        launch = ''
        if a.guest_uid == 501:
            subprocess.run([*ssh, '/var/jb/usr/bin/chown -R 501:501 ' + shlex.quote(guest_path)],
                           cwd=root, stdout=deploy_log, stderr=deploy_log, timeout=20, check=True)
            launch = shlex.quote(deployment_root + '/guest-exec') + ' 501 '
        command = (f'export IOS_KVM_NATIVE_GL_HOST=10.0.2.2 IOS_KVM_NATIVE_GL_PORT={a.port} '
                   f'IOS_KVM_NATIVE_GL_TOKEN_FILE={shlex.quote(guest_path + "/token")} '
                   f'IOS_KVM_NATIVE_SHARED={int(a.shared_ram)} IOS_KVM_NATIVE_BENCH={int(a.benchmark)} '
                   f'IOS_KVM_NATIVE_BENCH_PIXEL_STATE={int(a.benchmark_pixel_state)} '
                   f'IOS_KVM_NATIVE_ASYNC_READBACK={int(a.async_readback)} '
                   f'IOS_KVM_NATIVE_BENCH_ASYNC_READBACK={int(a.benchmark_async_readback)} '
                   f'IOS_KVM_NATIVE_BENCH_VERTEX_STATE={int(a.benchmark_vertex_state)} '
                   f'IOS_KVM_NATIVE_GL_CHECK_EVERY_CALL={int(a.eagl_check_every_call)}; '
                   f'{launch}{shlex.quote(guest_path + "/guest-check")}')
        result = subprocess.run([*ssh, command], cwd=root, capture_output=True, text=True, timeout=180 if a.benchmark else 45)
        output = result.stdout + result.stderr
        (a.output / 'guest.log').write_text(output)
        if result.returncode or 'Native GL general API check PASSED' not in output:
            raise RuntimeError('guest correctness check failed; inspect guest.log')
        for marker in ('Native client vertices PASSED','Native debug labels PASSED'):
            if marker not in output:
                raise RuntimeError('guest coverage did not finish: '+marker)
        if a.benchmark_vertex_state:
            if 'Native vertex state benchmark PASSED' not in output:
                raise RuntimeError('guest vertex-state benchmark did not finish')
            samples = [json.loads(line.split(': ', 1)[1]) for line in output.splitlines()
                       if line.startswith('Native vertex state benchmark: ')]
            if len(samples) != 28:
                raise RuntimeError('incomplete vertex-state benchmark samples')
            (a.output / 'vertex-state-benchmark.json').write_text(json.dumps(samples, indent=2) + '\n')
        if a.guest_uid == 501 and 'Native guest identity: uid=501 gid=501' not in output:
            raise RuntimeError('guest diagnostic identity was not verified')
        if a.shared_ram and 'Native shared IOSurface check PASSED' not in output:
            raise RuntimeError('Retained shared IOSurface check did not pass')
        if a.benchmark:
            if 'Native shared readback benchmark PASSED' not in output:
                raise RuntimeError('Shared readback benchmark did not finish')
            samples = [json.loads(line.split(': ', 1)[1]) for line in output.splitlines()
                       if line.startswith('Native readback benchmark: ')]
            if len(samples) != 80:
                raise RuntimeError('Incomplete shared readback benchmark samples')
            (a.output / 'benchmark.json').write_text(json.dumps(samples, indent=2) + '\n')
        renderer = next(line.split(': ', 1)[1] for line in output.splitlines() if line.startswith('Native proxy renderer: '))
        software = any(s in renderer.lower() for s in ('llvmpipe', 'softpipe', 'swrast'))
        if renderer.strip() != selected.get('renderer', '').strip():
            raise RuntimeError('actual renderer differs from selected profile')
        if software == bool(selected.get('hardware')):
            raise RuntimeError('actual hardware/software renderer disagrees with profile')
        race_command = command.replace(shlex.quote(guest_path + '/guest-check'),
                                       shlex.quote(guest_path + '/context-race-check'))
        race = subprocess.run([*ssh, race_command], cwd=root, capture_output=True, text=True, timeout=45)
        race_output = race.stdout + race.stderr
        (a.output / 'context-race.log').write_text(race_output)
        if race.returncode or 'Native GL context teardown race check PASSED' not in race_output:
            raise RuntimeError('guest context teardown race check failed')
        if a.guest_uid == 501 and 'Native guest identity: uid=501 gid=501' not in race_output:
            raise RuntimeError('guest race identity was not verified')
        if a.eagl:
            eagl_command = command.replace(shlex.quote(guest_path + '/guest-check'),
                                          shlex.quote(guest_path + '/eagl-check') + ' ' +
                                          shlex.quote(guest_path + '/libhakios-native-eagl.dylib'))
            eagl = subprocess.run([*ssh, eagl_command], cwd=root, capture_output=True, text=True, timeout=45)
            eagl_output = eagl.stdout + eagl.stderr
            (a.output / 'eagl.log').write_text(eagl_output)
            if eagl.returncode or 'Native EAGL macro ABI check PASSED' not in eagl_output:
                raise RuntimeError('isolated EAGL macro ABI check failed; inspect eagl.log')
            for marker in ('Native client vertices PASSED','Native debug labels PASSED','Native EAGL worker handoff PASSED'):
                if marker not in eagl_output:
                    raise RuntimeError('isolated EAGL coverage did not finish: '+marker)
            if a.benchmark_vertex_state:
                if 'Native vertex state benchmark PASSED' not in eagl_output:
                    raise RuntimeError('EAGL vertex-state benchmark did not finish')
                samples = [json.loads(line.split(': ', 1)[1]) for line in eagl_output.splitlines()
                           if line.startswith('Native vertex state benchmark: ')]
                if len(samples) != 28:
                    raise RuntimeError('incomplete EAGL vertex-state benchmark samples')
                (a.output / 'eagl-vertex-state-benchmark.json').write_text(json.dumps(samples, indent=2) + '\n')
            if a.guest_uid == 501 and 'Native guest identity: uid=501 gid=501' not in eagl_output:
                raise RuntimeError('guest EAGL identity was not verified')
            if a.shared_ram and 'Native EAGL shared IOSurface check PASSED' not in eagl_output:
                raise RuntimeError('isolated EAGL shared IOSurface check did not pass')
            policy = 'enabled' if a.eagl_check_every_call else 'disabled'
            if f'native per-call GL error checks {policy}' not in eagl_output:
                raise RuntimeError('isolated EAGL error-check policy did not match request')
            actual = next(line.split(': ', 1)[1] for line in eagl_output.splitlines()
                          if line.startswith('Native EAGL macro renderer: '))
            if actual.strip() != renderer.strip():
                raise RuntimeError('EAGL macro renderer differs from proxy renderer')
            receipt.update(eagl_macro_passed=True, eagl_renderer=actual)
        receipt.update(context_race_passed=True)
        receipt.update(passed=True, actual_renderer=renderer,
                       hashes={name: hashlib.sha256((local / name).read_bytes()).hexdigest()
                               for name in ['libios-kvm-native-gl.dylib', 'guest-check', 'context-race-check']})
        receipt['hashes']['server'] = hashlib.sha256(server_binary.read_bytes()).hexdigest()
        if a.guest_uid == 501:
            receipt['hashes']['guest-exec'] = hashlib.sha256((local / 'guest-exec').read_bytes()).hexdigest()
        if a.eagl:
            for name in ['eagl-check', 'eagl-coverage/libhakios-native-eagl.dylib']:
                receipt['hashes'][name] = hashlib.sha256((local / name).read_bytes()).hexdigest()
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
        if deployment_root:
            command = 'rm -rf -- ' + shlex.quote(deployment_root)
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
