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
header = mesa / 'virgl_vtest_winsys.h'
text = re.sub(r'int virgl_vtest_shared_get\([^;]+;\n*', '', header.read_text())
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
