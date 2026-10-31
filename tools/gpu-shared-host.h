/* ios-kvm experimental shared-RAM readback. Included by virglrenderer vtest. */
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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
   struct stat info;
   void *memory = MAP_FAILED;
   int seals = memfd && fd >= 0 ? fcntl(fd, F_GET_SEALS) : 0;
   bool sealed = !memfd || (seals >= 0 && (seals & (F_SEAL_GROW | F_SEAL_SHRINK)) ==
                                          (F_SEAL_GROW | F_SEAL_SHRINK));
   if (fd >= 0 && !fstat(fd, &info) && S_ISREG(info.st_mode) &&
       sealed && info.st_uid == geteuid() && !(info.st_mode & 077) && (uint64_t)info.st_size >= size)
      memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (fd >= 0) close(fd);
   int32_t status = -EINVAL;
   if (memory != MAP_FAILED) {
      uint64_t total = 0;
      bool valid = true;
      for (uint32_t i = 0; i < count; i++) {
         uint64_t physical = segments[i*2], length = segments[i*2+1];
         if (physical < 0x40000000 || !length || length > bytes ||
             physical - 0x40000000 > size || length > size - (physical - 0x40000000) ||
             total > bytes || length > bytes - total) {
            valid = false; break;
         }
         iov[i].iov_base = (char *)memory + (physical - 0x40000000);
         iov[i].iov_len = length;
         total += length;
      }
      struct vtest_resource *res = util_hash_table_get(ctx->resource_table, intptr_to_pointer(args.handle));
      if (valid && total == bytes && res)
         status = virgl_renderer_transfer_read_iov(res->res_id, ctx->ctx_id,
            args.level, args.stride, args.layer_stride, &args.box, args.offset, iov, count);
      munmap(memory, size);
   }
   free(segments); free(iov);
   static unsigned transfers;
   if (++transfers == 1 || transfers % 120 == 0 || status)
      fprintf(stderr, "ios-kvm shared: transfer %u, %u bytes, %u segments, status %d\n",
              transfers, bytes, count, status);
   return vtest_block_write(ctx->out_fd, &status, sizeof(status)) > 0 ? 0 : -EIO;
}
