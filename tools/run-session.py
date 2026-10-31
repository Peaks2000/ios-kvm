#!/usr/bin/env python3
"""Own the VM, validated renderer and loopback relay for one complete launch session."""
import fcntl
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from gpu_host import select_gpu

ROOT = Path(__file__).resolve().parents[1]

def port_free(port):
    with socket.socket() as check:
        check.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            check.bind(('127.0.0.1', port))
        except OSError:
            raise RuntimeError(f'Port {port} is already in use; stop the existing VM/helper session first')

def stop(process):
    if process is None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        process.wait()
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()

def main():
    local = ROOT / '.local'
    local.mkdir(exist_ok=True)
    with (local / 'vm-session.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('An ios-kvm session already owns this workspace')
        number = sys.argv[1] if len(sys.argv) > 1 else os.environ.get('QEMU_INSTANCE', '0')
        if number != '0':
            raise RuntimeError('Managed graphics currently supports instance 0; multiple concurrent VMs are pending')
        for port in (4444, 4445, 6712, int(os.environ.get('QEMU_SSH_PORT', '2222'))):
            port_free(port)
        profile = select_gpu(os.environ.get('IOS_KVM_GPU_MODE', 'auto'), os.environ.get('VTEST_RENDERNODE'))
        selected = profile.get('selected')
        if not selected:
            raise RuntimeError('No compatible renderer: ' + json.dumps(profile))
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + f'-{os.getpid()}'
        logs = local / 'sessions' / stamp
        logs.mkdir(parents=True, mode=0o700)
        profile_path = logs / 'gpu.json'
        profile_path.write_text(json.dumps(profile, indent=2) + '\n')
        print(f'ios-kvm: {selected["renderer"]}; {"hardware" if selected["hardware"] else "software fallback"}', flush=True)
        print(f'ios-kvm session logs: {logs}', flush=True)
        state = {'schema': 1, 'supervisor_pid': os.getpid(), 'logs': str(logs), 'renderer': selected['renderer']}
        state_path = local / 'vm-session.json'
        def save(status):
            state['status'] = status
            temporary = state_path.with_suffix('.tmp')
            temporary.write_text(json.dumps(state, indent=2) + '\n')
            temporary.chmod(0o600)
            temporary.replace(state_path)
        def interrupted(_signum, _frame):
            raise KeyboardInterrupt
        signal.signal(signal.SIGTERM, interrupted)
        children = {}
        result = 1
        with tempfile.TemporaryDirectory(prefix='ios-kvm-') as runtime, \
             (logs / 'renderer.log').open('w') as gpu_log, \
             (logs / 'relay.log').open('w') as relay_log, \
             (logs / 'vm.log').open('w') as vm_log:
            unix_socket = str(Path(runtime) / 'virgl.sock')
            try:
                children['relay'] = subprocess.Popen([sys.executable, str(ROOT / 'tools/virgl-relay.py'),
                    '--compressed', '--socket-path', unix_socket], stdout=relay_log, stderr=subprocess.STDOUT, start_new_session=True)
                environment = {**os.environ, 'IOS_KVM_SESSION_MANAGED': '1', 'QEMU_SHARED_RAM': '1', 'QEMU_INSTANCE': number}
                if not selected['hardware'] and 'VM_DISPLAY' not in environment:
                    environment['VM_DISPLAY'] = 'gtk,zoom-to-fit=on,show-cursor=on,grab-on-hover=off'
                children['vm'] = subprocess.Popen(['bash', str(ROOT / 'tools/run.sh'), number],
                    env=environment, stdout=vm_log, stderr=subprocess.STDOUT, start_new_session=True)
                children['renderer'] = subprocess.Popen([sys.executable, str(ROOT / 'tools/start-gpu-server.py'),
                    '--profile', str(profile_path), '--socket-path', unix_socket],
                    env=environment, stdout=gpu_log, stderr=subprocess.STDOUT, start_new_session=True)
                state['pids'] = {name: process.pid for name, process in children.items()}
                save('starting')
                while children['vm'].poll() is None:
                    for name in ('relay', 'renderer'):
                        if children[name].poll() is not None:
                            raise RuntimeError(f'{name} exited; see {logs / ("renderer.log" if name=="renderer" else "relay.log")}')
                    if Path(unix_socket).exists() and state['status'] == 'starting':
                        save('running')
                    time.sleep(.2)
                result = children['vm'].returncode
            except KeyboardInterrupt:
                result = 130
            finally:
                vm = children.get('vm')
                if vm and vm.poll() is None:
                    # Flush persistent guest storage before terminating the emulator when reachable.
                    try:
                        subprocess.run(['bash', str(ROOT / 'tools/ssh.sh'), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=2',
                            '/var/jb/usr/bin/sync'], timeout=5, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    except (OSError, subprocess.TimeoutExpired):
                        pass
                for name in ('vm', 'renderer', 'relay'):
                    stop(children.get(name))
                state['returncode'] = result
                save('stopped' if result in (0,130) else 'failed')
        return result

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (RuntimeError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'ios-kvm: {error}', file=sys.stderr)
        sys.exit(1)
