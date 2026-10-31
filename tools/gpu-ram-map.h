/* Reuse the mapping of sealed guest RAM within one host renderer process. */
#ifndef IOS_KVM_GPU_RAM_MAP_H
#define IOS_KVM_GPU_RAM_MAP_H
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>

struct ios_kvm_ram_view {
   void *address;
   uint64_t size;
   bool retained;
};

/* vtest handles requests sequentially. Each forked client owns its own cache.
 * The mapping keeps the inode alive after close(fd); matching dev/ino cannot
 * accidentally reuse a mapping of an already-destroyed RAM object. */
static struct {
   void *address;
   uint64_t size;
   dev_t device;
   ino_t inode;
} ios_kvm_ram_cache;

static inline void ios_kvm_ram_cache_clear(void)
{
   if (ios_kvm_ram_cache.address)
      munmap(ios_kvm_ram_cache.address, ios_kvm_ram_cache.size);
   ios_kvm_ram_cache.address = NULL;
}

static inline int ios_kvm_ram_map(int fd, uint64_t size, bool sealed_memfd,
                                struct ios_kvm_ram_view *view)
{
   struct stat info;
   *view = (struct ios_kvm_ram_view){0};
   if (!size || size > SIZE_MAX || fstat(fd, &info) || !S_ISREG(info.st_mode) ||
       info.st_uid != geteuid() || (info.st_mode & 077) || info.st_size < 0 ||
       (uint64_t)info.st_size < size)
      return -EINVAL;
   if (sealed_memfd) {
      int seals = fcntl(fd, F_GET_SEALS);
      if (seals < 0 || (seals & (F_SEAL_GROW | F_SEAL_SHRINK)) !=
                       (F_SEAL_GROW | F_SEAL_SHRINK))
         return -EINVAL;
      if (ios_kvm_ram_cache.address && ios_kvm_ram_cache.size == size &&
          ios_kvm_ram_cache.device == info.st_dev && ios_kvm_ram_cache.inode == info.st_ino) {
         *view = (struct ios_kvm_ram_view){ios_kvm_ram_cache.address, size, true};
         return 0;
      }
   }
   void *address = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (address == MAP_FAILED)
      return -errno;
   if (sealed_memfd) {
      ios_kvm_ram_cache_clear();
      ios_kvm_ram_cache.address = address;
      ios_kvm_ram_cache.size = size;
      ios_kvm_ram_cache.device = info.st_dev;
      ios_kvm_ram_cache.inode = info.st_ino;
   }
   *view = (struct ios_kvm_ram_view){address, size, sealed_memfd};
   return 0;
}

static inline void ios_kvm_ram_unmap(struct ios_kvm_ram_view *view)
{
   if (view->address && !view->retained)
      munmap(view->address, view->size);
   *view = (struct ios_kvm_ram_view){0};
}
#endif
