/* ios-kvm framed LZ4 transport. Included after the original raw I/O helpers. */
#include "lz4.h"

static int virgl_block_write(int fd, void *data, int size)
{
   uint32_t header[2] = { (uint32_t)size, 0 };
   char *packet = malloc((size_t)size + 8);
   if (!packet)
      return -ENOMEM;
   memcpy(packet, header, 8);
   memcpy(packet + 8, data, size);
   int ret = virgl_raw_write(fd, packet, size + 8);
   free(packet);
   return ret < 0 ? ret : size;
}

/* The winsys serializes reads on a connection. Keep partial frames by fd. */
static struct {
   char *bytes;
   uint32_t size, at;
} ios_kvm_frames[1024];

static int virgl_block_read(int fd, void *data, int size)
{
   if (fd < 0 || fd >= 1024)
      return -EINVAL;
   int done = 0;
   while (done < size) {
      if (ios_kvm_frames[fd].at == ios_kvm_frames[fd].size) {
         uint32_t header[2];
         if (virgl_raw_read(fd, header, 8) != 8)
            return -EIO;
         if (!header[0] || header[0] > (4u << 20) || header[1] > (4u << 20))
            return -EINVAL;
         free(ios_kvm_frames[fd].bytes);
         ios_kvm_frames[fd].bytes = malloc(header[0]);
         if (!ios_kvm_frames[fd].bytes)
            return -ENOMEM;
         if (header[1]) {
            char *compressed = malloc(header[1]);
            if (!compressed)
               return -ENOMEM;
            int ret = virgl_raw_read(fd, compressed, header[1]);
            int decoded = ret == (int)header[1] ? LZ4_decompress_safe(compressed,
               ios_kvm_frames[fd].bytes, header[1], header[0]) : -1;
            free(compressed);
            if (decoded != (int)header[0])
               return -EIO;
         } else if (virgl_raw_read(fd, ios_kvm_frames[fd].bytes, header[0]) != (int)header[0]) {
            return -EIO;
         }
         ios_kvm_frames[fd].size = header[0];
         ios_kvm_frames[fd].at = 0;
      }
      uint32_t available = ios_kvm_frames[fd].size - ios_kvm_frames[fd].at;
      uint32_t take = available < (uint32_t)(size - done) ? available : (uint32_t)(size - done);
      memcpy((char *)data + done, ios_kvm_frames[fd].bytes + ios_kvm_frames[fd].at, take);
      ios_kvm_frames[fd].at += take;
      done += take;
   }
   return size;
}
