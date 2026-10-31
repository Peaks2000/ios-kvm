#!/usr/bin/env python3
"""Loopback-only byte relay between QEMU user networking and VirGL vtest."""
import asyncio
import ctypes
import struct
import sys

compressed = '--compressed' in sys.argv
lz4 = ctypes.CDLL('liblz4.so.1')
lz4.LZ4_compressBound.argtypes = [ctypes.c_int]
lz4.LZ4_compress_default.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int]


async def client(reader, writer):
    remote_writer = None
    try:
        remote_reader, remote_writer = await asyncio.open_unix_connection('/tmp/ios-kvm-virgl.sock')

        async def copy(source, target):
            while data := await source.read(256 << 10):
                target.write(data)
                await target.drain()

        async def from_guest():
            while True:
                length, packed = struct.unpack('<II', await reader.readexactly(8))
                if not 0 < length <= 64 << 20 or packed:
                    raise ValueError('Invalid guest transport frame')
                remote_writer.write(await reader.readexactly(length))
                await remote_writer.drain()

        async def to_guest():
            while data := await remote_reader.read(1 << 20):
                bound = lz4.LZ4_compressBound(len(data))
                output = ctypes.create_string_buffer(bound)
                count = lz4.LZ4_compress_default(data, output, len(data), bound) if len(data) > 1024 else 0
                use = count > 0 and count < len(data)
                writer.write(struct.pack('<II', len(data), count if use else 0))
                writer.write(output.raw[:count] if use else data)
                await writer.drain()

        tasks = [asyncio.create_task(from_guest() if compressed else copy(reader, remote_writer)),
                 asyncio.create_task(to_guest() if compressed else copy(remote_reader, writer))]
        _done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        for task in pending:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
    except (ConnectionError, OSError, asyncio.IncompleteReadError) as error:
        print(error, flush=True)
    finally:
        writer.close()
        if remote_writer:
            remote_writer.close()


async def main():
    server = await asyncio.start_server(client, '127.0.0.1', 6712 if compressed else 6711)
    async with server:
        await server.serve_forever()


asyncio.run(main())
