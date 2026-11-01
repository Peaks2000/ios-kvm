/* ios-kvm framed LZ4 transport. Included after the original raw I/O helpers. */
#include "lz4.h"

static int virgl_block_write(int fd, void *data, int size)
{
   if (size <= 0)
      return size == 0 ? 0 : -EINVAL;
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

/* One complete command per socket write. Outer framing is a byte stream, so
 * combining the vtest header and body does not change the server protocol. */
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
static pthread_once_t ios_kvm_packets_once = PTHREAD_ONCE_INIT;
static int ios_kvm_packets_mode;
static const volatile uint32_t *ios_kvm_packets_control;
static int ios_kvm_packets_default_enabled(const char *mode)
{
   return !mode || !strcmp(mode, "auto") || !strcmp(mode, "1");
}
static void ios_kvm_packets_init(void)
{
   const char *mode = getenv("IOS_KVM_COMMAND_PACKETS");
   ios_kvm_packets_mode = ios_kvm_packets_default_enabled(mode);
   if (mode && !strcmp(mode, "marker")) {
      int fd = open("/var/tmp/ios-kvm-command-packets-control", O_RDONLY | O_NOFOLLOW);
      struct stat info;
      if (fd >= 0) {
         if (!fstat(fd, &info) && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
             !(info.st_mode & 077) && info.st_size == sizeof(uint32_t)) {
            void *control = mmap(NULL, sizeof(uint32_t), PROT_READ, MAP_SHARED, fd, 0);
            if (control != MAP_FAILED) ios_kvm_packets_control = control;
         }
         close(fd);
      }
   }
   fprintf(stderr, "ios-kvm command packets: mode=%s active=%d marker=%s\n",
           mode ? mode : "auto", ios_kvm_packets_mode,
           ios_kvm_packets_control ? "mapped" : "none");
}
static int ios_kvm_packets_enabled(void)
{
   pthread_once(&ios_kvm_packets_once, ios_kvm_packets_init);
   int enabled = ios_kvm_packets_mode || (ios_kvm_packets_control && *ios_kvm_packets_control == 1);
   static __thread int reported = -1;
   if (ios_kvm_packets_control && enabled != reported) {
      fprintf(stderr, "ios-kvm command packets: active=%d\n", enabled);
      reported = enabled;
   }
   return enabled;
}
static int ios_kvm_write_command(int fd, const void *header, int header_bytes,
                                const void *body, int body_bytes)
{
   if (header_bytes <= 0 || body_bytes < 0 || !header || (body_bytes && !body)) return -EINVAL;
   if (header_bytes > (64 << 20) || body_bytes > (64 << 20)) return -EMSGSIZE;
   if (!ios_kvm_packets_enabled() || (uint64_t)header_bytes + body_bytes > (4u << 20)) {
      int ret = virgl_block_write(fd, (void *)header, header_bytes);
      if (ret < 0) return ret;
      ret = virgl_block_write(fd, (void *)body, body_bytes);
      return ret < 0 ? ret : header_bytes + body_bytes;
   }
   uint32_t size = header_bytes + body_bytes;
   uint32_t envelope[2] = {size, 0};
   char *packet = malloc(size + sizeof(envelope));
   if (!packet) return -ENOMEM;
   memcpy(packet, envelope, sizeof(envelope));
   memcpy(packet + sizeof(envelope), header, header_bytes);
   if (body_bytes) memcpy(packet + sizeof(envelope) + header_bytes, body, body_bytes);
   int ret = virgl_raw_write(fd, packet, size + sizeof(envelope));
   free(packet);
   return ret < 0 ? ret : (int)size;
}

/* The winsys serializes reads on a connection. Keep partial frames by fd. */
static struct {
   char *bytes;
   uint32_t size, at;
} ios_kvm_frames[1024];

static void ios_kvm_reset_frames(int fd)
{
   if (fd < 0 || fd >= 1024)
      return;
   free(ios_kvm_frames[fd].bytes);
   ios_kvm_frames[fd].bytes = NULL;
   ios_kvm_frames[fd].size = 0;
   ios_kvm_frames[fd].at = 0;
}

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
