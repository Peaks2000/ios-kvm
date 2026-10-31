#!/usr/bin/env python3
"""Run fingerprint correctness/timing checks; --arm adds a standalone ARM ELF, never an iOS VM."""
import argparse
import ctypes
import ctypes.util
import json
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--arm', action='store_true', help='Also execute freestanding calculation under qemu-aarch64 user mode')
args = parser.parse_args()
(root / '.local').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='fingerprint-check-', dir=root / '.local') as directory:
    work = Path(directory)
    source = root / 'tools/test-surface-fingerprint.c'
    native = work / 'native'
    subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(native)], check=True)
    print('Native fingerprint test:', flush=True)
    subprocess.run([str(native)], check=True)
    reference_path = ctypes.util.find_library('xxhash')
    if reference_path:
        wrapper = work / 'reference.c'
        header = root / 'vendor/hakios/userland/src/surface_fingerprint.h'
        wrapper.write_text('#include ' + json.dumps(str(header)) + '\n'
                           'uint64_t test_hash(const void *data,size_t size){return surface_fingerprint(data,size);}\n')
        library = work / 'reference.so'
        subprocess.run(['cc', '-O2', '-shared', '-fPIC', str(wrapper), '-o', str(library)], check=True)
        actual = ctypes.CDLL(str(library)).test_hash
        reference = ctypes.CDLL(reference_path).XXH64
        actual.argtypes, actual.restype = [ctypes.c_void_p, ctypes.c_size_t], ctypes.c_uint64
        reference.argtypes, reference.restype = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint64], ctypes.c_uint64
        storage = ctypes.create_string_buffer(bytes((i*37+11)%256 for i in range(8192)))
        cases = 0
        for offset in range(8):
            for length in [*range(258),511,512,513,1023,1024,1025,4095,4096,4097]:
                address = ctypes.addressof(storage)+offset
                if actual(address,length) != reference(address,length,0):
                    raise SystemExit(f'Reference mismatch at offset={offset}, length={length}')
                cases += 1
        print(f'Independent system xxHash comparison passed: {cases} cases', flush=True)
    else:
        print('Independent system xxHash library unavailable; known-vector and mutation tests still ran.', flush=True)
    if args.arm:
        # Only these unused allocation declarations are needed from libc headers.
        # The calculation uses direct write/clock_gettime/exit Linux syscalls.
        includes = work / 'include'
        includes.mkdir()
        (includes / 'stdlib.h').write_text('#include <stddef.h>\nvoid *malloc(size_t);\nvoid free(void *);\n')
        (includes / 'string.h').write_text('#include <stddef.h>\nvoid *memcpy(void *,const void *,size_t);\nvoid *memset(void *,int,size_t);\n')
        arm = work / 'arm64'
        subprocess.run(['clang', '--target=aarch64-linux-gnu', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-ffreestanding', '-fno-stack-protector', '-nostdlib', '-static', '-fuse-ld=lld',
                        '-DIOS_KVM_FREESTANDING', '-Dmemcpy=__builtin_memcpy', '-I', str(includes),
                        str(source), '-Wl,-e,_start', '-o', str(arm)], check=True)
        print('Standalone ARM calculation (QEMU user mode, no VM):', flush=True)
        subprocess.run(['qemu-aarch64', '-cpu', 'cortex-a76', str(arm)], check=True)
