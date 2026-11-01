#!/usr/bin/env python3
"""Check tagged feature negotiation and shader drawing against real host servers; no VM."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import tempfile
import time
from virgl_draw import probe_draw

QUERY = 0x494b0000
REPLY = 0x494b0100
KNOWN = 3


def read(sock, size):
    result = bytearray()
    while len(result) < size:
        chunk = sock.recv(size - len(result))
        if not chunk:
            raise RuntimeError('Host disconnected during feature negotiation')
        result.extend(chunk)
    return bytes(result)


def query(sock, flags=QUERY):
    sock.sendall(struct.pack('<4I', 2, 7, 0, flags))
    header = struct.unpack('<2I', read(sock, 8))
    if header != (1, 7):
        raise RuntimeError(f'Unexpected busy-wait response: {header}')
    return struct.unpack('<I', read(sock, 4))[0]


def check(server, profile, ram, legacy, output, label):
    environment = dict(os.environ)
    environment.update(profile['selected']['environment'])
    for key in ('IOS_KVM_RAM_FILE', 'IOS_KVM_RAM_SIZE'):
        environment.pop(key, None)
    fd = None
    if ram:
        fd = os.memfd_create('ios-kvm-ram', os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
        os.fchmod(fd, 0o600)
        os.ftruncate(fd, 16 << 20)
        fcntl.fcntl(fd, fcntl.F_ADD_SEALS, fcntl.F_SEAL_GROW | fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_SEAL)
        environment.update(IOS_KVM_RAM_FILE=f'/proc/{os.getpid()}/fd/{fd}', IOS_KVM_RAM_SIZE=str(16 << 20))
    process = None
    try:
        with tempfile.TemporaryDirectory(prefix='ios-kvm-features-') as temporary, output.open('w') as log:
            path = str(Path(temporary) / 'socket')
            process = subprocess.Popen([str(server), '--use-egl-surfaceless', '--socket-path', path],
                env=environment, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            deadline = time.monotonic() + 15
            while not Path(path).exists():
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(f'{label}: host did not open its socket; inspect {output}')
                time.sleep(.05)
            with socket.socket(socket.AF_UNIX) as sock:
                sock.settimeout(15)
                sock.connect(path)
                name = b'ios-kvm-feature-check\0'
                sock.sendall(struct.pack('<2I', len(name), 8) + name)
                before = query(sock)  # Also works before caps/context initialization.
                expected = 0 if legacy or not ram else KNOWN
                features = before & KNOWN if before & 0xffffff00 == REPLY else 0
                if features != expected or legacy and before not in (0, 1):
                    raise RuntimeError(f'{label}: unexpected feature reply {before:#x}')
                sock.sendall(struct.pack('<2I', 0, 9))
                length, kind = struct.unpack('<2I', read(sock, 8))
                if kind != 2 or not 1 < length <= 65537:
                    raise RuntimeError(f'{label}: caps stream was corrupted')
                read(sock, length - 1)
                draw = probe_draw(sock)
                if not draw['ok']:
                    raise RuntimeError(f'{label}: shader pixels failed: {draw}')
                after = query(sock)
                normal = query(sock, 0)
                if (after not in (0, 1) if legacy else after != before) or normal not in (0, 1):
                    raise RuntimeError(f'{label}: normal busy-wait behavior changed')
                return {'label': label, 'reply': hex(before), 'features': features,
                        'normal_busy_reply': normal, 'draw': draw}
    finally:
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        if fd is not None:
            os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--legacy', type=Path, help='A preserved host binary without feature negotiation')
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    profile = json.loads(args.profile.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    cases = [(args.server.resolve(), True, False, 'updated-ram'),
             (args.server.resolve(), False, False, 'updated-no-ram')]
    if args.legacy:
        cases.append((args.legacy.resolve(), True, True, 'legacy-ram'))
    results = []
    for server, ram, legacy, label in cases:
        result = check(server, profile, ram, legacy,
                       args.output.with_name(args.output.stem + '-' + label + '.log'), label)
        results.append(result)
        print(json.dumps(result), flush=True)
    args.output.write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
