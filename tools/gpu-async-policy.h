#ifndef IOS_KVM_GPU_ASYNC_POLICY_H
#define IOS_KVM_GPU_ASYNC_POLICY_H
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifndef IOS_KVM_ASYNC_CONTROL_PATH
#define IOS_KVM_ASYNC_CONTROL_PATH "/var/tmp/ios-kvm-async-readback-control"
#endif
static pthread_once_t ios_kvm_async_once = PTHREAD_ONCE_INIT;
static int ios_kvm_async_mode;
static const uint32_t *ios_kvm_async_control;
static void ios_kvm_async_init(void)
{
   const char *mode = getenv("IOS_KVM_ASYNC_READBACK");
   ios_kvm_async_mode = mode && !strcmp(mode, "1");
   if (mode && !strcmp(mode, "marker")) {
      int fd = open(IOS_KVM_ASYNC_CONTROL_PATH, O_RDONLY | O_NOFOLLOW);
      struct stat info;
      if (fd >= 0) {
         if (!fstat(fd, &info) && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
             !(info.st_mode & 077) && info.st_size == sizeof(uint32_t)) {
            void *map = mmap(NULL, sizeof(uint32_t), PROT_READ, MAP_SHARED, fd, 0);
            if (map != MAP_FAILED) ios_kvm_async_control = map;
         }
         close(fd);
      }
   }
   fprintf(stderr, "ios-kvm async policy: mode=%s marker=%s\n", mode ? mode : "off",
           ios_kvm_async_control ? "mapped" : "none");
}
static int ios_kvm_async_requested(void)
{
   pthread_once(&ios_kvm_async_once, ios_kvm_async_init);
   int enabled = ios_kvm_async_mode || (ios_kvm_async_control &&
      __atomic_load_n(ios_kvm_async_control, __ATOMIC_ACQUIRE) == 1);
   static __thread int reported = -1;
   if (enabled != reported) {
      fprintf(stderr, "ios-kvm async policy: active=%d\n", enabled);
      reported = enabled;
   }
   return enabled;
}
#endif
