#!/usr/bin/env python3
"""Adapt Mesa's existing VirGL test transport for the iOS VM.

Uses protocol v0 (inline transfer payloads), because file descriptors cannot be
passed between an iOS guest and a Linux host. The local host relay remains bound
to loopback; QEMU user networking exposes it to the guest as 10.0.2.2.
"""
from pathlib import Path
import subprocess
import shutil

root = Path(__file__).resolve().parents[1]
mesa = root / '.local/mesa-25.0.7'
if '#define OSMESA_ES_PROFILE' not in (mesa / 'include/GL/osmesa.h').read_text():
    subprocess.run(['patch', '-p1', '-i', str(root / 'vendor/hakios/tools/mesa/osmesa-es-profile.patch')],
                   cwd=mesa, check=True)


def edit(name, old, new):
    path = mesa / name
    text = path.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f'Unexpected Mesa source: {name}')
    path.write_text(text.replace(old, new, 1))


edit('src/gallium/meson.build',
     "    subdir('winsys/virgl/drm')\n    subdir('winsys/virgl/vtest')",
     "    if system_has_kms_drm\n      subdir('winsys/virgl/drm')\n    endif\n    subdir('winsys/virgl/vtest')")
edit('src/gallium/drivers/virgl/meson.build',
     '  virgl_deps += libvirgldrm\n  virgl_deps += libvirglvtest',
     '  if system_has_kms_drm\n    virgl_deps += libvirgldrm\n  endif\n  virgl_deps += libvirglvtest')
edit('src/gallium/targets/osmesa/meson.build',
     'dep_ws2_32, dep_thread, dep_clock, dep_unwind, driver_swrast, idep_mesautil,',
     'dep_ws2_32, dep_thread, dep_clock, dep_unwind, driver_swrast, driver_virgl, idep_mesautil,')
edit('src/gallium/drivers/virgl/virgl_context.c',
     '#ifndef _WIN32\n#include <libsync.h>',
     '#if !defined(_WIN32) && !defined(__APPLE__)\n#include <libsync.h>')
edit('src/gallium/targets/osmesa/meson.build',
     'libmesa, libgallium, libws_null, osmesa_link_with,',
     'libmesa, libgallium, libgalliumvl_stub, libws_null, osmesa_link_with,')

path = mesa / 'src/gallium/winsys/virgl/vtest/virgl_vtest_socket.c'
source = path.read_text()
replacement = '''   struct sockaddr_in address = {0};
   int sock = socket(AF_INET, SOCK_STREAM, 0);
   if (sock < 0)
      return -1;
   address.sin_len = sizeof(address);
   address.sin_family = AF_INET;
   address.sin_port = htons(6711);
   address.sin_addr.s_addr = htonl(0x0a000202); /* QEMU user network: host */
   if (connect(sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
      close(sock);
      return -1;
   }
   int one = 1;
   setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
   vws->sock_fd = sock;
   virgl_vtest_send_init(vws);
   vws->protocol_version = virgl_vtest_negotiate_version(vws, 0);

'''
if 'QEMU user network: host' not in source:
    start = source.index('   struct sockaddr_un un;', source.index('int virgl_vtest_connect('))
    end = source.index('   return 0;\n}', start)
    source = source[:start] + replacement + source[end:]
    source = source.replace('#include <netinet/in.h>', '#include <netinet/in.h>\n#include <netinet/tcp.h>')
    path.write_text(source)

# Lossless framebuffer compression reduces the virtual-network readback cost.
source = path.read_text()
if '#include "gpu-transport.h"' not in source:
    source = source.replace('static int virgl_block_write(', 'static int virgl_raw_write(', 1)
    source = source.replace('static int virgl_block_read(', 'static int virgl_raw_read(', 1)
    source = source.replace('static int virgl_vtest_receive_fd(',
                            '#include "gpu-transport.h"\n\nstatic int virgl_vtest_receive_fd(', 1)
    source = source.replace('htons(6711)', 'htons(6712)')
    path.write_text(source)
vtest = path.parent
for name in ('lz4.c', 'lz4.h'):
    shutil.copyfile(root / 'vendor/lz4' / name, vtest / name)
lz4_source = vtest / 'lz4.c'
lz4_source.write_text(lz4_source.read_text().replace(
    'int LZ4_loadDict_internal(', 'static int LZ4_loadDict_internal(', 1))
shutil.copyfile(root / 'tools/gpu-transport.h', vtest / 'gpu-transport.h')
edit('src/gallium/winsys/virgl/vtest/virgl_vtest_socket.c',
     '   vws->sock_fd = sock;\n   virgl_vtest_send_init(vws);',
     '   vws->sock_fd = sock;\n   ios_kvm_reset_frames(sock);\n   virgl_vtest_send_init(vws);')
edit('src/gallium/winsys/virgl/vtest/meson.build',
     "['virgl_vtest_socket.c', 'virgl_vtest_winsys.c']",
     "['virgl_vtest_socket.c', 'virgl_vtest_winsys.c', 'lz4.c']")
edit('src/gallium/winsys/virgl/vtest/virgl_vtest_socket.c',
     '   line = malloc(stride);\n   while (hblocks)',
     '''   if (box->depth == 1 && stride == util_format_get_stride(format, box->width) &&
       data_size == stride * hblocks)
      return virgl_block_read(vws->sock_fd, data, data_size) < 0 ? -1 : 0;
   line = malloc(stride);
   while (hblocks)''')

sdk = root / '.local/sdks/iPhoneOS16.5.sdk'
(root / '.local/mesa-virgl-cross.ini').write_text(f'''[binaries]
c = ['clang', '-Qunused-arguments', '-target', 'arm64-apple-ios16.0', '-isysroot', '{sdk}', '-fuse-ld=lld']
cpp = ['clang++', '-Qunused-arguments', '-target', 'arm64-apple-ios16.0', '-isysroot', '{sdk}', '-fuse-ld=lld']
objc = ['clang', '-Qunused-arguments', '-target', 'arm64-apple-ios16.0', '-isysroot', '{sdk}', '-fuse-ld=lld']
ar = 'llvm-ar'
strip = 'llvm-strip'
pkg-config = 'false'
[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'
[properties]
needs_exe_wrapper = true
''')
print('Prepared iOS VirGL transport and cross-build configuration')
