/* ios-kvm experimental shared-RAM readback. Included by virglrenderer vtest. */
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "gpu-ram-map.h"

int vtest_ios_kvm_shared_get(uint32_t length_dw)
{
   struct vtest_context *ctx = vtest_get_current_context();
   struct vtest_transfer_args args;
   uint32_t bytes, count;
   int ret = vtest_transfer_decode_args(ctx, &args, &bytes);
   if (ret || ctx->input->read(ctx->input, &count, sizeof(count)) != sizeof(count))
      return -EINVAL;
   if (!bytes || bytes > (64u << 20) || !count || count > 4096 || length_dw != 12 + 4 * count)
      return -EINVAL;
   uint64_t *segments = malloc(count * 16);
   struct iovec *iov = calloc(count, sizeof(*iov));
   if (!segments || !iov) { free(segments); free(iov); return -ENOMEM; }
   if (ctx->input->read(ctx->input, segments, count * 16) != (int)(count * 16)) {
      free(segments); free(iov); return -EIO;
   }
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
         iov[i].iov_base = (char *)mapping.address + (physical - 0x40000000);
         iov[i].iov_len = length;
         total += length;
      }
      struct vtest_resource *res = util_hash_table_get(ctx->resource_table, intptr_to_pointer(args.handle));
      if (valid && total == bytes && res)
         status = virgl_renderer_transfer_read_iov(res->res_id, ctx->ctx_id,
            args.level, args.stride, args.layer_stride, &args.box, args.offset, iov, count);
      ios_kvm_ram_unmap(&mapping);
   }
   free(segments); free(iov);
   static unsigned transfers;
   if (++transfers == 1 || transfers % 120 == 0 || status)
      fprintf(stderr, "ios-kvm shared: transfer %u, %u bytes, %u segments, status %d\n",
              transfers, bytes, count, status);
   return vtest_block_write(ctx->out_fd, &status, sizeof(status)) > 0 ? 0 : -EIO;
}
