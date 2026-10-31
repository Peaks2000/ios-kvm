#!/usr/bin/env python3
"""Preload guest RAM into an anonymous shared-memory file, then launch QEMU's wrapper."""
import errno
import fcntl
import os
import sys

source, size = sys.argv[1], int(sys.argv[2])
command = sys.argv[3:]
if not command or size <= 0:
    raise SystemExit('usage: ram-memfd.py pristine-ram size command [args ...]')
fd = os.memfd_create('ios-kvm-ram', os.MFD_ALLOW_SEALING)
os.fchmod(fd, 0o600)
os.ftruncate(fd, size)
# Copy only allocated extents, preserving the large holes in the pristine boot layout.
with open(source, 'rb', buffering=0) as image:
    end = min(size, os.fstat(image.fileno()).st_size)
    position = 0
    while position < end:
        try:
            start = os.lseek(image.fileno(), position, os.SEEK_DATA)
        except OSError as error:
            if error.errno == errno.ENXIO:
                break
            raise
        stop = min(end, os.lseek(image.fileno(), start, os.SEEK_HOLE))
        at = start
        while at < stop:
            data = os.pread(image.fileno(), min(16 << 20, stop - at), at)
            if not data:
                raise SystemExit('Unexpected end of pristine RAM image')
            written = 0
            while written < len(data):
                written += os.pwrite(fd, data[written:], at + written)
            at += len(data)
        position = stop
fcntl.fcntl(fd, fcntl.F_ADD_SEALS, fcntl.F_SEAL_GROW | fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_SEAL)
os.set_inheritable(fd, True)
# This process becomes the launch wrapper and retains the descriptor until QEMU exits.
os.environ['QEMU_MEMFD_PATH'] = f'/proc/{os.getpid()}/fd/{fd}'
os.execvp(command[0], command)
