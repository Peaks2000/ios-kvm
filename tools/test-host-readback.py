#!/usr/bin/env python3
"""Exercise real VirGL shared-RAM transfers with a synthetic RAM object, without a VM."""
import argparse
import fcntl
import mmap
import os
from pathlib import Path
import signal
import socket
import statistics
import struct
import subprocess
import tempfile
import time
from gpu_host import select_gpu
from virgl_draw import build_draw_encoder, expected_pixel, pixels_match


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--mode', choices=('auto', 'hardware', 'software'), default='auto')
    parser.add_argument('--frames', type=int, default=24)
    parser.add_argument('--draw', action='store_true', help='Submit real TGSI vertex/fragment shaders and alpha-blended triangles')
    args = parser.parse_args()
    if args.frames < 5:
        parser.error('--frames must be at least 5')
    profile = select_gpu(args.mode)['selected']
    if not profile:
        raise RuntimeError('No validated host renderer')
    encoder = build_draw_encoder() if args.draw else None
    width, height = 750, 1624
    byte_count = width * height * 4
    first, split, gap = 4096, byte_count // 2 + 7, 65536
    second = first + split + gap
    span = second + byte_count - split + 4096
    ram_size = 6 << 30
    fd = os.memfd_create('ios-kvm-ram', os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
    try:
        os.fchmod(fd, 0o600)
        os.ftruncate(fd, ram_size)
        fcntl.fcntl(fd, fcntl.F_ADD_SEALS, fcntl.F_SEAL_GROW | fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_SEAL)
        with mmap.mmap(fd, span) as memory, tempfile.TemporaryDirectory(prefix='ios-kvm-readback-') as directory:
            memory[:] = b'\xa5' * span
            path = str(Path(directory) / 'vtest.sock')
            environment = {**os.environ, **profile['environment'],
                           'IOS_KVM_RAM_FILE': f'/proc/{os.getpid()}/fd/{fd}', 'IOS_KVM_RAM_SIZE': str(ram_size)}
            with tempfile.TemporaryFile() as log:
                process = subprocess.Popen([str(args.server.resolve()), '--use-egl-surfaceless', '--socket-path', path],
                    env=environment, stdout=log, stderr=log, start_new_session=True)
                try:
                    deadline = time.monotonic() + 10
                    while not Path(path).exists():
                        if process.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError('Renderer failed to open socket')
                        time.sleep(.05)
                    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                        connection.settimeout(10)
                        connection.connect(path)
                        def send(command, data=b'', length=None):
                            connection.sendall(struct.pack('<II', len(data)//4 if length is None else length, command) + data)
                        def read(size):
                            result = bytearray()
                            while len(result) < size:
                                data = connection.recv(size-len(result))
                                if not data:
                                    raise RuntimeError('Renderer disconnected')
                                result.extend(data)
                            return bytes(result)
                        name = b'ios-kvm-readback-test\0'
                        send(8, name, len(name))
                        send(9)
                        length, kind = struct.unpack('<II', read(8))
                        if kind != 2 or not 1 < length <= 65537:
                            raise RuntimeError('Invalid capabilities')
                        read(length-1)
                        send(2, struct.pack('<10I', 1, 2, 67, 10, width, height, 1, 1, 0, 0))
                        if args.draw:
                            send(2, struct.pack('<10I', 2, 0, 64, 16, 96, 1, 1, 1, 0, 0))
                        transfer = struct.pack('<11I', 1, 0, width*4, 0, 0, 0, 0, width, height, 1, byte_count)
                        segments = struct.pack('<4Q', 0x40000000+first, split,
                                               0x40000000+second, byte_count-split)
                        shared = transfer + struct.pack('<I', 2) + segments
                        samples = []
                        for frame in range(args.frames):
                            pixel = bytes((frame*13 % 256, frame*29 % 256, frame*37 % 256, 255))
                            if args.draw:
                                commands = subprocess.check_output([str(encoder),str(frame),str(width),str(height)])
                                send(6, commands)
                                pixel = expected_pixel(frame)
                            else:
                                send(5, transfer + pixel * (width*height), 11)
                            payload = pixel * (width*height)
                            start = time.perf_counter_ns()
                            send(64, shared)
                            status, = struct.unpack('<i', read(4))
                            elapsed = (time.perf_counter_ns()-start)/1000
                            if status:
                                raise RuntimeError(f'Transfer failed: {status}')
                            actual = memory[first:first+split] + memory[second:second+byte_count-split]
                            if actual != payload:
                                # Shader interpolation/blend quantization may round by one unit.
                                mismatch = not args.draw or not pixels_match(actual,pixel)
                                if mismatch:
                                    raise RuntimeError(f'Pixel mismatch on fresh frame {frame}: expected {list(pixel)}, first {list(actual[:4])}')
                            if memory[:first] != b'\xa5'*first or memory[first+split:second] != b'\xa5'*gap or \
                               memory[span-4096:] != b'\xa5'*4096:
                                raise RuntimeError('Padding guard overwritten')
                            if frame >= 4:
                                samples.append(elapsed)
                        invalid = transfer + struct.pack('<I', 2) + struct.pack('<4Q', 0, split, 0x40000000+second, byte_count-split)
                        send(64, invalid)
                        if struct.unpack('<i', read(4))[0] >= 0:
                            raise RuntimeError('Out-of-range RAM segment accepted')
                        send(64, shared)
                        if struct.unpack('<i', read(4))[0]:
                            raise RuntimeError('Valid transfer failed after rejected segment')
                        print(f'host shared {"shader draw" if args.draw else "upload"} readback: renderer={profile["renderer"]} frames={args.frames} '
                              f'bytes={byte_count} segments=2 median_us={statistics.median(samples):.1f} '
                              'fresh_pixels=pass padding=pass invalid_range=pass recovery=pass')
                except Exception:
                    log.seek(0)
                    print(log.read(8192).decode(errors='replace'))
                    raise
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
    finally:
        os.close(fd)


if __name__ == '__main__':
    main()
