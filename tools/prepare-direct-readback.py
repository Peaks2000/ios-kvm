#!/usr/bin/env python3
"""Prepare an opt-in Gallium readback directly into a temporarily pinned caller buffer."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
mesa = root / '.local/mesa-25.0.7/src'

def edit(relative, old, new):
    path = mesa / relative
    text = path.read_text()
    if new.startswith('static int\nvirgl_vtest_direct_readback(') and 'static int\nvirgl_vtest_direct_readback(' in text:
        return
    if new.startswith('static int\nvirgl_direct_readback(') and 'static int\nvirgl_direct_readback(' in text:
        return
    if new.startswith('   /* ios-kvm: preserve Mesa') and '   /* ios-kvm: preserve Mesa' in text:
        return
    if new in text:
        return
    if text.count(old) != 1:
        raise SystemExit(f'Unexpected direct-readback source: {path}')
    path.write_text(text.replace(old, new, 1))

edit('gallium/include/pipe/p_context.h', 'struct pipe_context {', '''struct pipe_context {
   /* ios-kvm: 1 complete, 0 unsupported, -1 transport error. No caller memory retained. */
   int (*ios_kvm_readback)(struct pipe_context *, struct pipe_resource *,
                           const struct pipe_box *, unsigned stride, void *pixels);
''')
edit('gallium/drivers/virgl/virgl_winsys.h', 'struct virgl_winsys {', '''struct virgl_winsys {
   int (*ios_kvm_readback)(struct virgl_winsys *, struct virgl_hw_res *,
                           const struct pipe_box *, unsigned stride, void *pixels);
''')
header = mesa / 'gallium/winsys/virgl/vtest/virgl_vtest_winsys.h'
text = header.read_text()
# The winsys base must stay at offset zero: the existing helpers cast its pointer.
text = text.replace('struct virgl_vtest_winsys {\n   void *ios_kvm_direct_pin;', 'struct virgl_vtest_winsys {')
text = re.sub(r'int virgl_vtest_direct_get\([^;]+;\n*', '', text)
text = re.sub(r'void virgl_vtest_direct_release\([^;]+;\n*', '', text)
header.write_text(text)
edit('gallium/winsys/virgl/vtest/virgl_vtest_winsys.h', '   struct virgl_winsys base;',
     '   struct virgl_winsys base;\n   void *ios_kvm_direct_pin;')
edit('gallium/winsys/virgl/vtest/virgl_vtest_winsys.h', 'int virgl_vtest_connect(',
     '''int virgl_vtest_direct_get(struct virgl_vtest_winsys *, uint32_t handle,
                          const struct pipe_box *, uint32_t stride, uint32_t bytes, void *pixels);
void virgl_vtest_direct_release(struct virgl_vtest_winsys *);
int virgl_vtest_connect(''')
edit('gallium/winsys/virgl/vtest/virgl_vtest_winsys.c',
     'static void virgl_hw_res_destroy(', '''static int
virgl_vtest_direct_readback(struct virgl_winsys *vws, struct virgl_hw_res *res,
                           const struct pipe_box *box, unsigned stride, void *pixels)
{
   struct virgl_vtest_winsys *vtws = virgl_vtest_winsys(vws);
   const char *enabled = getenv("IOS_KVM_DIRECT_READBACK");
   if (!enabled || strcmp(enabled, "1") || vtws->protocol_version != 0 ||
       box->depth != 1 || box->height <= 0 || box->width <= 0 ||
       (res->format != PIPE_FORMAT_B8G8R8A8_UNORM && res->format != PIPE_FORMAT_R8G8B8A8_UNORM) ||
       (uint64_t)box->width * 4 > stride)
      return 0;
   uint64_t bytes = (uint64_t)stride * (box->height - 1) + (uint64_t)box->width * 4;
   if (bytes < 65536 || bytes > (64u << 20)) return 0;
   /* A caller may free/reuse its buffer immediately after glReadPixels. Never cache this pin. */
   struct virgl_hw_res temporary = {0};
   int result = virgl_vtest_shared_get(vtws, res->res_handle, 0, stride, 0,
                                      box, bytes, pixels, &temporary);
   virgl_vtest_shared_release(&temporary);
   return result;
}

static void virgl_hw_res_destroy(''')
edit('gallium/winsys/virgl/vtest/virgl_vtest_winsys.c',
     '   const char *enabled = getenv("IOS_KVM_DIRECT_READBACK");',
     '''   const char *enabled = getenv("IOS_KVM_DIRECT_READBACK");
   static unsigned direct_checks;
   if (++direct_checks == 1)
      fprintf(stderr, "ios-kvm direct winsys: enabled=%s protocol=%u box=%d,%d,%d format=%u block=%u stride=%u\\n",
              enabled ? enabled : "unset", vtws->protocol_version, box->width, box->height, box->depth,
              res->format, util_format_get_blocksize(res->format), stride);''')
edit('gallium/winsys/virgl/vtest/virgl_vtest_winsys.c',
     '(res->format != PIPE_FORMAT_B8G8R8A8_UNORM && res->format != PIPE_FORMAT_R8G8B8A8_UNORM) ||',
     '''(util_format_get_blocksize(res->format) != 4 || util_format_get_blockwidth(res->format) != 1 ||
        util_format_get_blockheight(res->format) != 1) ||''')
edit('gallium/winsys/virgl/vtest/virgl_vtest_winsys.c',
     '''   /* A caller may free/reuse its buffer immediately after glReadPixels. Never cache this pin. */
   struct virgl_hw_res temporary = {0};
   int result = virgl_vtest_shared_get(vtws, res->res_handle, 0, stride, 0,
                                      box, bytes, pixels, &temporary);
   virgl_vtest_shared_release(&temporary);
   return result;''',
     '''   /* Reuse the connection, but release the caller's pin before returning. */
   int result = virgl_vtest_direct_get(vtws, res->res_handle, box, stride, bytes, pixels);
   static unsigned calls;
   if (++calls == 1 || result < 0)
      fprintf(stderr, "ios-kvm direct: %u bytes, status %d\\n", (unsigned)bytes, result);
   return result;''')
edit('gallium/winsys/virgl/vtest/virgl_vtest_winsys.c',
     '   virgl_resource_cache_flush(&vtws->cache);',
     '   virgl_vtest_direct_release(vtws);\n   virgl_resource_cache_flush(&vtws->cache);')
edit('gallium/winsys/virgl/vtest/virgl_vtest_winsys.c',
     '   vtws->base.transfer_get = virgl_vtest_transfer_get;',
     '   vtws->base.transfer_get = virgl_vtest_transfer_get;\n   vtws->base.ios_kvm_readback = virgl_vtest_direct_readback;')
edit('gallium/drivers/virgl/virgl_resource.c',
     'void virgl_init_context_resource_functions(struct pipe_context *ctx)', '''static int
virgl_direct_readback(struct pipe_context *ctx, struct pipe_resource *resource,
                      const struct pipe_box *box, unsigned stride, void *pixels)
{
   struct virgl_winsys *vws = virgl_screen(ctx->screen)->vws;
   struct virgl_resource *res = virgl_resource(resource);
   const char *enabled = getenv("IOS_KVM_DIRECT_READBACK");
   if (!enabled || strcmp(enabled, "1") || !vws->ios_kvm_readback || res->use_staging ||
       resource->target != PIPE_TEXTURE_2D || resource->nr_samples > 1)
      return 0;
   ctx->flush(ctx, NULL, 0);
   vws->resource_wait(vws, res->hw_res);
   return vws->ios_kvm_readback(vws, res->hw_res, box, stride, pixels);
}

void virgl_init_context_resource_functions(struct pipe_context *ctx)''')
edit('gallium/drivers/virgl/virgl_resource.c',
     '   struct virgl_resource *res = virgl_resource(resource);\n   const char *enabled',
     '''   struct virgl_resource *res = virgl_resource(resource);
   static unsigned direct_attempts;
   if (++direct_attempts == 1)
      fprintf(stderr, "ios-kvm direct driver: staging=%u target=%u samples=%u callback=%u\\n",
              res->use_staging, resource->target, resource->nr_samples, vws->ios_kvm_readback != NULL);
   const char *enabled''')
edit('gallium/drivers/virgl/virgl_resource.c',
     '    ctx->buffer_map = virgl_resource_transfer_map;',
     '    ctx->ios_kvm_readback = virgl_direct_readback;\n    ctx->buffer_map = virgl_resource_transfer_map;')
path = 'mesa/state_tracker/st_cb_readpixels.c'
edit(path, '   if (!st->prefer_blit_based_texture_transfer) {',
     '''   if (!st->prefer_blit_based_texture_transfer &&
       !(pipe->ios_kvm_readback && getenv("IOS_KVM_DIRECT_READBACK") &&
         !strcmp(getenv("IOS_KVM_DIRECT_READBACK"), "1"))) {''')
edit(path, '   /* map resources */', '''   /* ios-kvm: preserve Mesa's format conversion/flip blit, skip the guest staging memcpy. */
   if (pipe->ios_kvm_readback && !pack->BufferObj && !pack->Invert && !pack->SwapBytes &&
       type == GL_UNSIGNED_BYTE && (format == GL_RGBA || format == GL_BGRA)) {
      int stride = _mesa_image_row_stride(pack, width, format, type);
      if (stride >= (int64_t)width * 4 && height > 0 && width > 0) {
         struct pipe_box box = {dst_x, dst_y, 0, width, height, 1};
         void *destination = _mesa_image_address2d(pack, pixels, width, height, format, type, 0, 0);
         int result = pipe->ios_kvm_readback(pipe, dst, &box, stride, destination);
         if (result != 0) {
            pipe_resource_reference(&dst, NULL);
            if (result < 0) _mesa_error(ctx, GL_INVALID_OPERATION, "ios-kvm direct readback failed");
            return;
         }
      }
   }

   /* map resources */''')
edit(path, '   /* Cache a staging texture for back-to-back ReadPixels, to avoid CPU-GPU',
     '''   /* ios-kvm: already matching FBOs need neither a staging blit nor a guest memcpy. */
   if (pipe->ios_kvm_readback && !pack->BufferObj && !pack->Invert && !pack->SwapBytes &&
       type == GL_UNSIGNED_BYTE && (format == GL_RGBA || format == GL_BGRA) &&
       rb->surface->u.tex.level == 0 && rb->surface->u.tex.first_layer == 0 &&
       _mesa_fb_orientation(ctx->ReadBuffer) != Y_0_TOP &&
       _mesa_format_matches_format_and_type(rb->Format, format, type, false, NULL)) {
      int stride = _mesa_image_row_stride(pack, width, format, type);
      if (width > 0 && height > 0 && stride >= (int64_t)width * 4) {
         struct pipe_box box = {.x=x, .y=y, .z=0, .width=width, .height=height, .depth=1};
         void *destination = _mesa_image_address2d(pack, pixels, width, height, format, type, 0, 0);
         int result = pipe->ios_kvm_readback(pipe, src, &box, stride, destination);
         if (result != 0) {
            if (result < 0) _mesa_error(ctx, GL_INVALID_OPERATION, "ios-kvm direct FBO readback failed");
            return;
         }
      }
   }

   /* Cache a staging texture for back-to-back ReadPixels, to avoid CPU-GPU''')
edit(path, 'struct pipe_box box = {dst_x, dst_y, 0, width, height, 1};',
     'struct pipe_box box = {.x=dst_x, .y=dst_y, .z=0, .width=width, .height=height, .depth=1};')
edit(path, '''      if (_mesa_format_matches_format_and_type(rb->Format, format,
                                               type, pack->SwapBytes, NULL)) {''', '''      if (!(pipe->ios_kvm_readback && getenv("IOS_KVM_DIRECT_READBACK") &&
            !strcmp(getenv("IOS_KVM_DIRECT_READBACK"), "1")) &&
          _mesa_format_matches_format_and_type(rb->Format, format,
                                               type, pack->SwapBytes, NULL)) {''')
print('Prepared opt-in direct caller-buffer readback')
