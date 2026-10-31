#!/usr/bin/env python3
"""Probe real EGL renderers and select a hardware-first, pixel-validated host profile."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import signal
import socket
import struct
import tempfile
import time
import shlex

ROOT = Path(__file__).resolve().parents[1]

def build_probe():
    source = ROOT / 'tools/gpu-probe.c'
    binary = ROOT / '.local/gpu-probe'
    if not binary.exists() or source.stat().st_mtime_ns > binary.stat().st_mtime_ns:
        binary.parent.mkdir(parents=True, exist_ok=True)
        flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'egl', 'gl'], text=True))
        temporary = binary.with_name(f'gpu-probe.{os.getpid()}.tmp')
        try:
            subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(temporary), *flags], check=True)
            temporary.replace(binary)
        finally:
            temporary.unlink(missing_ok=True)
    return binary

def probe(binary, arguments, environment=None):
    try:
        result = subprocess.run([str(binary), *arguments], env=environment, text=True,
                                capture_output=True, timeout=15)
        value = json.loads(result.stdout)
        if result.returncode or isinstance(value, dict) and not value.get('ok'):
            return None, value
        return value, None
    except (subprocess.TimeoutExpired, OSError, ValueError) as error:
        return None, {'error': str(error)}

def software_environment():
    # GLVND may otherwise retain NVIDIA even when LIBGL_ALWAYS_SOFTWARE is set.
    # Find the installed Mesa ICD rather than assuming a distribution-specific basename.
    directories = (Path('/usr/share/glvnd/egl_vendor.d'), Path('/etc/glvnd/egl_vendor.d'))
    for directory in directories:
        for path in sorted(directory.glob('*.json')):
            try:
                library = json.loads(path.read_text())['ICD']['library_path']
                if 'mesa' in library.casefold():
                    return {'LIBGL_ALWAYS_SOFTWARE': '1', '__EGL_VENDOR_LIBRARY_FILENAMES': str(path)}
            except (OSError, KeyError, ValueError):
                continue
    return {'LIBGL_ALWAYS_SOFTWARE': '1'}

def is_software(renderer):
    return any(name in renderer.casefold() for name in ('llvmpipe', 'softpipe', 'swrast', 'swiftshader', 'software rasterizer'))

def probe_vtest(profile):
    """Check the real host server's context/caps on a private socket without touching a VM."""
    server = ROOT / '.local/virgl-build/vtest/virgl_test_server'
    if not server.is_file():
        return {'ok': False, 'error': 'Host virgl_test_server has not been built'}
    with tempfile.TemporaryDirectory(prefix='vtest-probe-', dir=ROOT / '.local') as directory:
        path = str(Path(directory) / 'socket')
        with tempfile.TemporaryFile() as log:
            process = subprocess.Popen([str(server), '--use-egl-surfaceless', '--socket-path', path],
                env={**os.environ, **profile['environment']}, stdout=log, stderr=log, start_new_session=True)
            try:
                deadline = time.monotonic() + 10
                while not Path(path).exists():
                    if process.poll() is not None:
                        raise RuntimeError('Host renderer exited before opening its socket')
                    if time.monotonic() >= deadline:
                        raise TimeoutError('Host renderer socket did not appear')
                    time.sleep(.05)
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                    connection.settimeout(10)
                    connection.connect(path)
                    name = b'ios-kvm-host-probe\0'
                    connection.sendall(struct.pack('<II', len(name), 8) + name)  # CREATE_RENDERER
                    connection.sendall(struct.pack('<II', 0, 9))  # GET_CAPS2, protocol zero
                    def read(length):
                        output = bytearray()
                        while len(output) < length:
                            data = connection.recv(length-len(output))
                            if not data:
                                raise RuntimeError('Host renderer closed during capability negotiation')
                            output.extend(data)
                        return bytes(output)
                    length, kind = struct.unpack('<II', read(8))
                    if kind != 2 or not 1 < length <= 65537:
                        raise RuntimeError('Invalid host renderer capability reply')
                    caps = read(length-1)
                    # The renderer is a 64-byte NUL-terminated field in the caps. Verify that
                    # the real server selected the same renderer as the EGL pixel probe.
                    expected = profile['renderer'].encode()[:63] + b'\0'
                    # VirGL deliberately rewrites llvmpipe to LLVMPIPE for old guest clients.
                    offset = caps.lower().find(expected.lower())
                    if offset < 0:
                        raise RuntimeError('VirGL selected a different renderer from the EGL probe')
                    actual = caps[offset:offset+64].split(b'\0', 1)[0].decode(errors='replace')
                    return {'ok': True, 'caps_bytes': len(caps), 'renderer': actual}
            except (OSError, RuntimeError) as error:
                log.seek(0)
                return {'ok': False, 'error': str(error), 'log': log.read(4096).decode(errors='replace')}
            finally:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()

def select_gpu(mode='auto', render_node=None):
    if mode not in ('auto', 'hardware', 'software'):
        raise ValueError('IOS_KVM_GPU_MODE must be auto, hardware or software')
    if mode == 'software' and render_node:
        raise ValueError('A render node cannot be combined with software mode')
    binary = build_probe()
    candidates, failures = [], []
    seen = set()
    if mode != 'software':
        devices, error = probe(binary, ['--list'])
        if error:
            failures.append(error)
        for device in devices or []:
            if render_node and device.get('render_node') != render_node:
                continue
            result, error = probe(binary, ['--device', str(device['index'])])
            if error:
                failures.append({'device': device, **error})
                continue
            key = (result.get('render_node'), result['renderer'])
            if key in seen:
                continue
            seen.add(key)
            result['hardware'] = not is_software(result['renderer'])
            result['environment'] = {}
            if result.get('render_node'):
                result['environment']['VTEST_RENDERNODE'] = result['render_node']
            candidates.append(result)
        if not candidates and not render_node:
            result, error = probe(binary, ['--default'])
            if result:
                result['hardware'] = not is_software(result['renderer'])
                result['environment'] = {}
                candidates.append(result)
            elif error:
                failures.append(error)
    hardware = None
    for candidate in candidates:
        if candidate['hardware']:
            candidate['vtest'] = probe_vtest(candidate)
            if candidate['vtest']['ok']:
                hardware = candidate
                break
            failures.append(candidate['vtest'])
    if hardware:
        selected = hardware
    elif mode != 'hardware' and not render_node:
        overrides = software_environment()
        result, error = probe(binary, ['--default'], {**os.environ, **overrides})
        if result and is_software(result['renderer']):
            result['hardware'] = False
            result['environment'] = overrides
            result['vtest'] = probe_vtest(result)
            candidates.append(result)
            selected = result if result['vtest']['ok'] else None
            if not selected:
                failures.append(result['vtest'])
        else:
            failures.append(error or {'error': 'Software request did not produce a software renderer', 'probe': result})
            selected = None
    else:
        selected = None
    return {'schema': 1, 'mode': mode, 'selected': selected, 'candidates': candidates, 'failures': failures}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('auto', 'hardware', 'software'), default=os.environ.get('IOS_KVM_GPU_MODE', 'auto'))
    parser.add_argument('--render-node', default=os.environ.get('VTEST_RENDERNODE'))
    args = parser.parse_args()
    if args.mode == 'software' and args.render_node:
        parser.error('--render-node cannot be combined with software mode')
    try:
        result = select_gpu(args.mode, args.render_node)
    except (OSError, subprocess.CalledProcessError) as error:
        result = {'schema': 1, 'selected': None, 'error': str(error)}
    print(json.dumps(result, indent=2))
    return 0 if result.get('selected') else 1

if __name__ == '__main__':
    sys.exit(main())
