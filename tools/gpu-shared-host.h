/* ios-kvm experimental shared-RAM readback. Included by virglrenderer vtest. */
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "gpu-ram-map.h"

static int vtest_ios_kvm_shared_transfer(uint32_t length_dw, bool upload, bool asynchronous)
{
   struct vtest_context *ctx = vtest_get_current_context();
   struct vtest_transfer_args args;
   uint32_t bytes, count;
   int ret = vtest_transfer_decode_args(ctx, &args, &bytes);
   if (ret || ctx->input->read(ctx->input, &count, sizeof(count)) != sizeof(count))
      return -EINVAL;
   if (!bytes || bytes > (64u << 20) || !count || count > 4096 || length_dw != 12 + 4 * count + (asynchronous ? 3 : 0))
      return -EINVAL;
   uint64_t *segments = malloc(count * 16);
   struct iovec *iov = calloc(count, sizeof(*iov));
   if (!segments || !iov) { free(segments); free(iov); return -ENOMEM; }
   if (ctx->input->read(ctx->input, segments, count * 16) != (int)(count * 16)) {
      free(segments); free(iov); return -EIO;
   }
   uint32_t trailer[3] = {0};
   if (asynchronous && ctx->input->read(ctx->input, trailer, sizeof(trailer)) != sizeof(trailer)) {
      free(segments); free(iov); return -EIO;
   }
   uint64_t completion = (uint64_t)trailer[0] | ((uint64_t)trailer[1] << 32);
   const char *path = getenv("IOS_KVM_RAM_FILE");
   const char *size_string = getenv("IOS_KVM_RAM_SIZE");
   uint64_t size = size_string ? strtoull(size_string, NULL, 10) : 0;
   /* Our launcher keeps memfd alive via /proc/PID/fd/N; ordinary files must not be symlinks. */
   bool memfd = false;
   if (path && !strncmp(path, "/proc/", 6)) {
      char target[256];
      ssize_t n = readlink(path, target, sizeof(target)-1);
      if (n > 0) {
         target[n] = 0;
         memfd = !strcmp(target, "/memfd:ios-kvm-ram (deleted)");
      }
   }
   int fd = path && size ? open(path, O_RDWR | O_CLOEXEC | (memfd ? 0 : O_NOFOLLOW)) : -1;
   struct ios_kvm_ram_view mapping = {0};
   int mapped = fd >= 0 ? ios_kvm_ram_map(fd, size, memfd, &mapping) : -EINVAL;
   if (fd >= 0) close(fd);
   int32_t status = -EINVAL;
   bool completion_valid = !asynchronous || (!mapped && trailer[2] && !(completion & 7) &&
      completion >= 0x40000000 && size >= 8 && completion - 0x40000000 <= size - 8);
   if (!mapped) {
      uint64_t total = 0;
      bool valid = true;
      for (uint32_t i = 0; i < count; i++) {
         uint64_t physical = segments[i*2], length = segments[i*2+1];
         if (physical < 0x40000000 || !length || length > bytes ||
             physical - 0x40000000 > size || length > size - (physical - 0x40000000) ||
             total > bytes || length > bytes - total) {
            valid = false; break;
         }
         if (asynchronous && completion_valid &&
             completion < physical + length && physical < completion + 8) completion_valid = false;
         iov[i].iov_base = (char *)mapping.address + (physical - 0x40000000);
         iov[i].iov_len = length;
         total += length;
      }
      struct vtest_resource *res = util_hash_table_get(ctx->resource_table, intptr_to_pointer(args.handle));
      if (valid && completion_valid && total == bytes && res) {
         if (upload)
            status = virgl_renderer_transfer_write_iov(res->res_id, ctx->ctx_id,
               args.level, args.stride, args.layer_stride, &args.box, args.offset, iov, count);
         else
            status = virgl_renderer_transfer_read_iov(res->res_id, ctx->ctx_id,
               args.level, args.stride, args.layer_stride, &args.box, args.offset, iov, count);
      }
      if (asynchronous && completion_valid) {
         int32_t *cell = (int32_t *)((char *)mapping.address + completion - 0x40000000);
         __atomic_store_n(cell, status, __ATOMIC_RELAXED);
         __atomic_store_n((uint32_t *)(cell + 1), trailer[2], __ATOMIC_RELEASE);
      }
      ios_kvm_ram_unmap(&mapping);
   }
   free(segments); free(iov);
   static unsigned uploads, readbacks;
   unsigned sequence = upload ? ++uploads : ++readbacks;
   if (sequence == 1 || sequence % 120 == 0 || status)
      fprintf(stderr, "ios-kvm shared: %s transfer %u, %u bytes, %u segments, status %d\n",
              asynchronous ? "async-readback" : upload ? "upload" : "readback", sequence, bytes, count, status);
   if (asynchronous) return mapped || !completion_valid ? -EINVAL : 0;
   return vtest_block_write(ctx->out_fd, &status, sizeof(status)) > 0 ? 0 : -EIO;
}

int vtest_ios_kvm_shared_get(uint32_t length_dw)
{
   return vtest_ios_kvm_shared_transfer(length_dw, false, false);
}

int vtest_ios_kvm_shared_put(uint32_t length_dw)
{
   return vtest_ios_kvm_shared_transfer(length_dw, true, false);
}

int vtest_ios_kvm_shared_get_async(uint32_t length_dw)
{
   return vtest_ios_kvm_shared_transfer(length_dw, false, true);
}
