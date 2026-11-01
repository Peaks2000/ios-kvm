#!/usr/bin/env python3
"""Add the experimental pinned-RAM readback command to our local Mesa/vtest builds."""
from pathlib import Path
import shutil
import re
import subprocess

root = Path(__file__).resolve().parents[1]
mesa = root / '.local/mesa-25.0.7/src/gallium/winsys/virgl/vtest'
host = root / '.local/virglrenderer/vtest'

def edit(path, old, new):
    text = path.read_text()
    if old == '   flags = bw_buf[VCMD_BUSY_WAIT_FLAGS];' and 'goto ios_kvm_feature_reply;' in text:
        return
    if old == 'int virgl_vtest_connect(struct virgl_vtest_winsys *vws)' and 'static int ios_kvm_query_features(' in text:
        return
    if old == '   virgl_vtest_send_transfer_put(vtws, res->res_handle,' and 'virgl_vtest_shared_put(vtws,' in text:
        return
    if old == 'int vtest_transfer_get(UNUSED uint32_t length_dw)' and '#include "gpu-shared-host.h"' in text:
        return
    if new in text:
        return
    if old not in text:
        raise SystemExit(f'Unexpected source: {path}')
    path.write_text(text.replace(old, new, 1))

shutil.copyfile(root / 'tools/gpu-shared-guest.h', mesa / 'gpu-shared-guest.h')
shutil.copyfile(root / 'tools/gpu-shared-request.h', mesa / 'gpu-shared-request.h')
shutil.copyfile(root / 'tools/gpu-shared-host.h', host / 'gpu-shared-host.h')
shutil.copyfile(root / 'tools/gpu-ram-map.h', host / 'gpu-ram-map.h')
edit(mesa / 'virgl_vtest_socket.c', '#include "gpu-transport.h"',
     '#include "gpu-transport.h"\n#include "gpu-shared-guest.h"')
for name, old, new in (
    ('virgl_vtest_winsys.h', 'uint32_t bytes, void *destination);',
     'uint32_t bytes, void *destination, struct virgl_hw_res *resource);'),
    ('virgl_vtest_winsys.c', 'layer_stride, box, size, ptr + buf_offset);',
     'layer_stride, box, size, ptr + buf_offset, res);'),
):
    path = mesa / name
    path.write_text(path.read_text().replace(old, new))
prototype = '''int virgl_vtest_shared_get(struct virgl_vtest_winsys *vws,
                          uint32_t handle, uint32_t level, uint32_t stride,
                          uint32_t layer_stride, const struct pipe_box *box,
                          uint32_t bytes, void *destination, struct virgl_hw_res *resource);
'''
prototype += 'int virgl_vtest_shared_upload_enabled(struct virgl_vtest_winsys *vws);\n'
header = mesa / 'virgl_vtest_winsys.h'
text = re.sub(r'int virgl_vtest_shared_get\([^;]+;\n*', '', header.read_text())
text = re.sub(r'int virgl_vtest_shared_put\([^;]+;\n*', '', text)
text = re.sub(r'int virgl_vtest_shared_upload_enabled\([^;]+;\n*', '', text)
text = re.sub(r'void virgl_vtest_shared_release\([^;]+;\n*', '', text)
text = text.replace('int virgl_vtest_connect(', prototype +
                    'void virgl_vtest_shared_release(struct virgl_hw_res *resource);\n\nint virgl_vtest_connect(', 1)
header.write_text(text)
edit(mesa / 'virgl_vtest_winsys.h', '   void *ptr;', '   void *ios_kvm_pin;\n   void *ptr;')
edit(mesa / 'virgl_vtest_winsys.c', '   virgl_vtest_send_resource_unref(vtws, res->res_handle);',
     '   virgl_vtest_shared_release(res);\n   virgl_vtest_send_resource_unref(vtws, res->res_handle);')
edit(mesa / 'virgl_vtest_winsys.c',
     '   virgl_vtest_send_transfer_get(vtws, res->res_handle,',
     '''   if (vtws->protocol_version == 0 && box->depth == 1 &&
       valid_stride == util_format_get_stride(res->format, box->width)) {
      ptr = virgl_vtest_resource_map(vws, res);
      int shared = virgl_vtest_shared_get(vtws, res->res_handle, level, stride,
                                         layer_stride, box, size, ptr + buf_offset, res);
      virgl_vtest_resource_unmap(vws, res);
      if (shared != 0)
         return shared > 0 ? 0 : -1;
   }
   virgl_vtest_send_transfer_get(vtws, res->res_handle,''')
# IOKit is used only by the Darwin transport, and libOSMesa carries the dependency.
edit(root / '.local/mesa-25.0.7/src/gallium/targets/osmesa/meson.build',
     'libosmesa = shared_library(',
     "osmesa_link_args += ['-framework', 'IOKit', '-framework', 'CoreFoundation']\n\nlibosmesa = shared_library(")
edit(host / 'vtest_renderer.c', 'int vtest_transfer_get(UNUSED uint32_t length_dw)',
     '#include "gpu-shared-host.h"\n\nint vtest_transfer_get(UNUSED uint32_t length_dw)')
edit(host / 'vtest.h', 'int vtest_transfer_get(uint32_t length_dw);',
     'int vtest_ios_kvm_shared_get(uint32_t length_dw);\nint vtest_transfer_get(uint32_t length_dw);')
edit(host / 'vtest_server.c', '   HANDLER(GET_CAPS,                send_caps,             false),',
     '   [64] = {"IOS_KVM_SHARED_GET", vtest_ios_kvm_shared_get, true},\n'
     '   HANDLER(GET_CAPS,                send_caps,             false),')
print('Prepared experimental shared-RAM GPU transfer')
subprocess.run(['python3', str(root / 'tools/prepare-direct-readback.py')], check=True)

# Shared uploads use a separate command on a negotiated, updated host renderer.
edit(mesa / 'virgl_vtest_winsys.h',
     'void virgl_vtest_shared_release(struct virgl_hw_res *resource);',
     'int virgl_vtest_shared_put(struct virgl_vtest_winsys *vws,\n                          uint32_t handle, uint32_t level, uint32_t stride,\n                          uint32_t layer_stride, const struct pipe_box *box,\n                          uint32_t bytes, void *source, struct virgl_hw_res *resource);\nvoid virgl_vtest_shared_release(struct virgl_hw_res *resource);')
edit(mesa / 'virgl_vtest_winsys.c',
     '   virgl_vtest_send_transfer_put(vtws, res->res_handle,',
     '   const char *upload = getenv("IOS_KVM_SHARED_UPLOAD");\n   static unsigned upload_checks;\n   if (upload && !strcmp(upload, "1") && size >= 65536 && upload_checks++ < 3)\n      fprintf(stderr, "ios-kvm upload check: format=%u block=%u bytes=%u stride=%u width=%d height=%d protocol=%u\\n",\n              res->format, util_format_get_blocksize(res->format), size, valid_stride,\n              box->width, box->height, vtws->protocol_version);\n   if (upload && !strcmp(upload, "1") && size >= IOS_KVM_SHARED_UPLOAD_MIN_BYTES && vtws->protocol_version == 0 && box->depth == 1 &&\n       valid_stride == util_format_get_stride(res->format, box->width) &&\n       util_format_get_blocksize(res->format) == 4 &&\n       util_format_get_blockwidth(res->format) == 1 && util_format_get_blockheight(res->format) == 1) {\n      ptr = virgl_vtest_resource_map(vws, res);\n      int shared = virgl_vtest_shared_put(vtws, res->res_handle, level, stride,\n                                         layer_stride, box, size, ptr + buf_offset, res);\n      virgl_vtest_resource_unmap(vws, res);\n      if (shared != 0)\n         return shared > 0 ? 0 : -1;\n   }\n   virgl_vtest_send_transfer_put(vtws, res->res_handle,')
edit(host / 'vtest.h', 'int vtest_ios_kvm_shared_get(uint32_t length_dw);',
     'int vtest_ios_kvm_shared_get(uint32_t length_dw);\nint vtest_ios_kvm_shared_put(uint32_t length_dw);')
edit(host / 'vtest_server.c',
     '   [64] = {"IOS_KVM_SHARED_GET", vtest_ios_kvm_shared_get, true},',
     '   [64] = {"IOS_KVM_SHARED_GET", vtest_ios_kvm_shared_get, true},\n'
     '   [65] = {"IOS_KVM_SHARED_PUT", vtest_ios_kvm_shared_put, true},')
edit(mesa / 'virgl_vtest_socket.c', '   address.sin_port = htons(6712);',
     '   unsigned long port = 6712;\n   const char *port_text = getenv("IOS_KVM_VTEST_PORT");\n   if (port_text) {\n      char *end;\n      port = strtoul(port_text, &end, 10);\n      if (end == port_text || *end || !port || port > 65535) { errno = EINVAL; return -1; }\n   }\n   address.sin_port = htons((uint16_t)port);')

edit(mesa / 'virgl_vtest_winsys.c', '#include "virgl_vtest_winsys.h"',
     '#include "virgl_vtest_winsys.h"\n#include "gpu-shared-request.h"')

# Negotiate before using the extension; explicit mode1 also requires support.
edit(mesa / 'virgl_vtest_winsys.h', '   unsigned protocol_version;',
     '   unsigned protocol_version;\n   uint32_t ios_kvm_features;')
edit(mesa / 'virgl_vtest_winsys.c',
     '   const char *upload = getenv("IOS_KVM_SHARED_UPLOAD");',
     '   int upload = virgl_vtest_shared_upload_enabled(vtws);')
path = mesa / 'virgl_vtest_winsys.c'
path.write_text(path.read_text().replace('upload && !strcmp(upload, "1") &&', 'upload &&'))
shutil.copyfile(root / 'tools/gpu-shared-request.h', host / 'gpu-shared-request.h')
edit(host / 'vtest_renderer.c', '#include "gpu-shared-host.h"',
     '#include "gpu-shared-host.h"\n#include "gpu-shared-request.h"')
edit(host / 'vtest_renderer.c',
     '   flags = bw_buf[VCMD_BUSY_WAIT_FLAGS];',
     '   flags = bw_buf[VCMD_BUSY_WAIT_FLAGS];\n   if (length_dw == VCMD_BUSY_WAIT_SIZE && bw_buf[VCMD_BUSY_WAIT_HANDLE] == 0 &&\n       (uint32_t)flags == IOS_KVM_FEATURE_QUERY_FLAGS) {\n      const char *ram_file = getenv("IOS_KVM_RAM_FILE");\n      const char *ram_size = getenv("IOS_KVM_RAM_SIZE");\n      reply_buf[0] = IOS_KVM_FEATURE_REPLY_BASE;\n      if (ram_file && *ram_file && ram_size && strtoull(ram_size, NULL, 10))\n         reply_buf[0] |= IOS_KVM_FEATURE_UPLOAD | IOS_KVM_FEATURE_SYNC_READBACK;\n      goto ios_kvm_feature_reply;\n   }')
edit(host / 'vtest_renderer.c',
     '   hdr_buf[VTEST_CMD_LEN] = 1;\n   hdr_buf[VTEST_CMD_ID] = VCMD_RESOURCE_BUSY_WAIT;\n   reply_buf[0] = busy ? 1 : 0;',
     '   reply_buf[0] = busy ? 1 : 0;\nios_kvm_feature_reply:\n   hdr_buf[VTEST_CMD_LEN] = 1;\n   hdr_buf[VTEST_CMD_ID] = VCMD_RESOURCE_BUSY_WAIT;')
edit(mesa / 'virgl_vtest_socket.c', 'int virgl_vtest_connect(struct virgl_vtest_winsys *vws)',
     'static int ios_kvm_query_features(struct virgl_vtest_winsys *vws)\n{\n   uint32_t header[2] = {VCMD_BUSY_WAIT_SIZE, VCMD_RESOURCE_BUSY_WAIT};\n   uint32_t request[2] = {0, IOS_KVM_FEATURE_QUERY_FLAGS};\n   uint32_t reply;\n   if (virgl_block_write(vws->sock_fd, header, sizeof(header)) != sizeof(header) ||\n       virgl_block_write(vws->sock_fd, request, sizeof(request)) != sizeof(request) ||\n       virgl_block_read(vws->sock_fd, header, sizeof(header)) != sizeof(header) ||\n       header[0] != 1 || header[1] != VCMD_RESOURCE_BUSY_WAIT ||\n       virgl_block_read(vws->sock_fd, &reply, sizeof(reply)) != sizeof(reply))\n      return -1;\n   vws->ios_kvm_features = ios_kvm_decode_features(reply);\n   fprintf(stderr, "ios-kvm host features: shared uploads %s\\n",\n           vws->ios_kvm_features & IOS_KVM_FEATURE_UPLOAD ? "supported" : "unavailable");\n   return 0;\n}\n\nint virgl_vtest_connect(struct virgl_vtest_winsys *vws)')
edit(mesa / 'virgl_vtest_socket.c',
     '   vws->protocol_version = virgl_vtest_negotiate_version(vws, 0);',
     '   vws->protocol_version = virgl_vtest_negotiate_version(vws, 0);\n   if (ios_kvm_query_features(vws)) {\n      close(sock);\n      ios_kvm_reset_frames(sock);\n      vws->sock_fd = -1;\n      return -1;\n   }')

# Upgrade previously prepared negotiation code without duplicating functions.
path = host / 'vtest_renderer.c'
path.write_text(path.read_text().replace('reply_buf[0] |= IOS_KVM_FEATURE_UPLOAD;',
    'reply_buf[0] |= IOS_KVM_FEATURE_UPLOAD | IOS_KVM_FEATURE_SYNC_READBACK;'))
edit(mesa / 'virgl_vtest_socket.c',
     '   vws->ios_kvm_features = ios_kvm_decode_features(reply);',
     '   vws->ios_kvm_features = ios_kvm_decode_features(reply);\n'
     '   vws->base.ios_kvm_readback_sync = !!(vws->ios_kvm_features & IOS_KVM_FEATURE_SYNC_READBACK);')
