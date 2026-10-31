/* Exercise the production mapping helper with real Linux memfds, without QEMU. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
static unsigned maps, unmaps;
static void *tracked_mmap(void *address, size_t size, int prot, int flags, int fd, off_t offset)
{
   maps++;
   return mmap(address, size, prot, flags, fd, offset);
}
static int tracked_munmap(void *address, size_t size)
{
   unmaps++;
   return munmap(address, size);
}
#define mmap tracked_mmap
#define munmap tracked_munmap
#include "gpu-ram-map.h"
#undef mmap
#undef munmap

static int ram(uint64_t size, bool sealed)
{
   int fd = memfd_create("ios-kvm-ram", MFD_CLOEXEC | MFD_ALLOW_SEALING);
   assert(fd >= 0 && !fchmod(fd, 0600) && !ftruncate(fd, size));
   if (sealed)
      assert(!fcntl(fd, F_ADD_SEALS, F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL));
   return fd;
}

static void checks(void)
{
   struct ios_kvm_ram_view a, b;
   int fd = ram(8192, true);
   assert(!ios_kvm_ram_map(fd, 8192, true, &a));
   unsigned first_maps = maps;
   memset(a.address, 0x39, 8192);
   ios_kvm_ram_unmap(&a);
   assert(!ios_kvm_ram_map(fd, 8192, true, &b) && maps == first_maps);
   for (unsigned i = 0; i < 8192; i++) assert(((unsigned char *)b.address)[i] == 0x39);
   ios_kvm_ram_unmap(&b);
   assert(ftruncate(fd, 4096) == -1 && errno == EPERM);
   assert(ios_kvm_ram_map(fd, 8193, true, &a) < 0);
   assert(ios_kvm_ram_map(fd, 0, true, &a) < 0);
   assert(ios_kvm_ram_map(-1, 8192, true, &a) < 0);
   assert(!fchmod(fd, 0644));
   assert(ios_kvm_ram_map(fd, 8192, true, &a) < 0); /* Validate even cached hits. */
   assert(!fchmod(fd, 0600));
   int duplicate = dup(fd);
   assert(duplicate >= 0);
   close(fd);
   assert(!ios_kvm_ram_map(duplicate, 8192, true, &a) && maps == first_maps);
   ios_kvm_ram_unmap(&a);
   close(duplicate);
   int other = ram(8192, true);
   assert(!ios_kvm_ram_map(other, 8192, true, &a));
   assert(maps == first_maps + 1 && unmaps == 1);
   for (unsigned i = 0; i < 8192; i++) assert(!((unsigned char *)a.address)[i]);
   ios_kvm_ram_unmap(&a);
   close(other);
   ios_kvm_ram_cache_clear();
   assert(unmaps == 2);
   fd = ram(8192, false);
   assert(ios_kvm_ram_map(fd, 8192, true, &a) < 0); /* Unsealed RAM cannot be retained. */
   assert(!ios_kvm_ram_map(fd, 8192, false, &a) && !a.retained);
   memset(a.address, 0x71, 8192);
   ios_kvm_ram_unmap(&a);
   assert(!ios_kvm_ram_map(fd, 8192, false, &a));
   assert(((unsigned char *)a.address)[8191] == 0x71);
   ios_kvm_ram_unmap(&a);
   close(fd);
   char name[] = "/tmp/ios-kvm-map-test-XXXXXX";
   fd = mkstemp(name);
   assert(fd >= 0 && !unlink(name) && !ftruncate(fd, 8192));
   assert(ios_kvm_ram_map(fd, 8192, true, &a) < 0);
   assert(!ios_kvm_ram_map(fd, 8192, false, &a) && !a.retained);
   ios_kvm_ram_unmap(&a);
   close(fd);
   puts("mapping checks passed: reuse, inode replacement, fd lifetime, seals, bounds, permissions, transient fallback");
}

static double now(void)
{
   struct timespec t;
   assert(!clock_gettime(CLOCK_MONOTONIC, &t));
   return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
static int compare(const void *a, const void *b)
{
   double x = *(const double *)a, y = *(const double *)b;
   return (x > y) - (x < y);
}
static void bench(void)
{
   const uint64_t size = UINT64_C(6) << 30;
   const size_t bytes = 750 * 1624 * 4, offset = 512 * 1024 * 1024 + 12345;
   unsigned char *frame = malloc(bytes);
   assert(frame);
   int fd = ram(size, true);
   for (unsigned phase = 0; phase < 4; phase++) {
      bool retained = phase == 1 || phase == 2; /* ABBA */
      double samples[120];
      for (unsigned i = 0; i < 124; i++) {
         memset(frame, (unsigned char)(i + phase), bytes);
         struct ios_kvm_ram_view view;
         double start = now();
         assert(!ios_kvm_ram_map(fd, size, retained, &view));
         memcpy((char *)view.address + offset, frame, bytes);
         ios_kvm_ram_unmap(&view);
         double elapsed = now() - start;
         if (i >= 4) samples[i - 4] = elapsed;
      }
      struct ios_kvm_ram_view verify;
      assert(!ios_kvm_ram_map(fd, size, true, &verify));
      assert(!memcmp((char *)verify.address + offset, frame, bytes));
      ios_kvm_ram_unmap(&verify);
      qsort(samples, 120, sizeof(*samples), compare);
      printf("mapping bench phase=%u retained=%u median_us=%.1f p95_us=%.1f bytes=%zu\n",
             phase, retained, (samples[59] + samples[60]) / 2, samples[113], bytes);
      ios_kvm_ram_cache_clear();
   }
   free(frame);
   close(fd);
}
int main(int argc, char **argv)
{
   checks();
   if (argc == 2 && !strcmp(argv[1], "--bench")) bench();
   return 0;
}
