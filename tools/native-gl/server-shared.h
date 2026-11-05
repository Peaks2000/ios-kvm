/* Map once before threads start; requests never replace the mapping. */
#include "../gpu-ram-map.h"
static struct ios_kvm_ram_view ng_ram;
#ifdef NG_SHARED_TEST_HOOKS
#include <signal.h>
/* Compiled only into the process-exit diagnostic server. Stop after validating
 * and rendering a retained request, before touching its destination memory. */
static void ng_shared_test_pause(void) {
  static _Atomic int stopped;
  if (atomic_exchange(&stopped, 1))
    return;
  const char *text = getenv("IOS_KVM_NATIVE_TEST_NOTIFY_FD");
  char *end;
  long fd = text ? strtol(text, &end, 10) : -1;
  if (!text || !*text || *end || fd < 3 || fd > INT32_MAX)
    _exit(80);
  const char ready = 'R';
  ssize_t n;
  do {
    n = write((int)fd, &ready, 1);
  } while (n < 0 && errno == EINTR);
  if (n != 1 || raise(SIGSTOP))
    _exit(81);
}
#endif
struct ng_segment {
  uint64_t physical, length;
};
static int ng_ram_init(void) {
  const char *path = getenv("IOS_KVM_RAM_FILE"),
             *size_text = getenv("IOS_KVM_RAM_SIZE");
  if (!path && !size_text)
    return 0;
  if (!path || !size_text)
    return -1;
  char *end;
  uint64_t size = strtoull(size_text, &end, 10);
  if (!*size_text || *end || !size || size > (UINT64_C(64) << 30))
    return -1;
  char target[256];
  ssize_t n = readlink(path, target, sizeof(target) - 1);
  if (n <= 0)
    return -1;
  target[n] = 0;
  if (strcmp(target, "/memfd:ios-kvm-ram (deleted)"))
    return -1;
  int fd = open(path, O_RDWR | O_CLOEXEC);
  if (fd < 0)
    return -1;
  int result = ios_kvm_ram_map(fd, size, true, &ng_ram);
  close(fd);
  return result;
}
static int ng_ram_range(uint64_t physical, uint64_t bytes) {
  return ng_ram.address && physical >= UINT64_C(0x40000000) && bytes &&
         physical - UINT64_C(0x40000000) <= ng_ram.size &&
         bytes <= ng_ram.size - (physical - UINT64_C(0x40000000));
}
static int ng_segment_compare(const void *left, const void *right) {
  uint64_t a = ((const struct ng_segment *)left)->physical,
           b = ((const struct ng_segment *)right)->physical;
  return (a > b) - (a < b);
}
static int ng_shared_read(const uint8_t *data, size_t size, void *output,
                          size_t capacity, size_t *used) {
  if (size < 11 * 8 || *used)
    return -1;
  uint64_t a[11];
  memcpy(a, data, sizeof(a));
  uint64_t completion = a[9], sequence = a[10];
  if (!sequence || sequence > UINT32_MAX || (completion & 7) ||
      !ng_ram_range(completion, 8))
    return -1;
  int32_t status = -EINVAL;
  int result = -1;
  struct ng_segment *segments = NULL, *sorted = NULL;
  uint8_t *pixels = NULL;
  if (!a[8] || a[8] > 4096 || size != sizeof(a) + a[8] * 16 || !a[7] ||
      a[7] > (64u << 20))
    goto complete;
  size_t count = a[8];
  segments = malloc(count * sizeof(*segments));
  sorted = malloc(count * sizeof(*sorted));
  if (!segments || !sorted) {
    status = -ENOMEM;
    goto complete;
  }
  memcpy(segments, data + sizeof(a), count * sizeof(*segments));
  uint64_t total = 0;
  for (size_t i = 0; i < count; i++) {
    uint64_t physical = segments[i].physical, n = segments[i].length;
    if (!ng_ram_range(physical, n) || n > a[7] - total)
      goto complete;
    if (completion < physical + n && physical < completion + 8)
      goto invalid_cell;
    total += n;
  }
  if (total != a[7])
    goto complete;
  memcpy(sorted, segments, count * sizeof(*sorted));
  qsort(sorted, count, sizeof(*sorted), ng_segment_compare);
  for (size_t i = 1; i < count; i++)
    if (sorted[i].physical < sorted[i - 1].physical + sorted[i - 1].length)
      goto complete;
  size_t tight = 0;
  if ((a[4] != GL_RGBA && a[4] != NG_BGRA) || a[5] != GL_UNSIGNED_BYTE ||
      ng_pixels(a[2], a[3], a[4], a[5], &tight) || !a[2] || !a[3] ||
      a[6] < a[2] * 4 || a[6] > (64u << 20) || (a[6] & 3) ||
      a[6] * (a[3] - 1) + a[2] * 4 != a[7])
    goto complete;
  if (capacity < 4)
    goto complete;
  pixels = calloc(1, tight);
  if (!pixels) {
    status = -ENOMEM;
    goto complete;
  }
  ng_errors_collect();
  GLint old[4];
  if (ng_pixel_state(1, old))
    goto complete;
  ng_trace_draw("readback", 0, 0, a[2] * a[3]);
  glReadPixels((GLint)a[0], (GLint)a[1], a[2], a[3], GL_RGBA, a[5], pixels);
  int failed = ng_errors_collect();
  ng_pixel_restore(1, old);
  if (ng_errors_collect())
    failed = 1;
  if (failed) {
    status = -EIO;
    goto complete;
  }
  ng_gpu_frame_capture((GLint)a[0], (GLint)a[1], a[2], a[3], completion,
                        (uint32_t)sequence);
  ng_trace_pixels(a[2], a[3], pixels, completion, sequence);
  if (a[4] == NG_BGRA)
    ng_swap_rb(pixels, pixels, tight);
#ifdef NG_SHARED_TEST_HOOKS
  ng_shared_test_pause();
#endif
  /* Only active pixel ranges are written; row padding stays untouched. */
  size_t part = 0;
  uint64_t base = 0;
  for (uint64_t y = 0; y < a[3]; y++) {
    uint64_t offset = y * a[6], left = a[2] * 4;
    const uint8_t *from = pixels + y * a[2] * 4;
    while (part < count && offset >= base + segments[part].length) {
      base += segments[part].length;
      part++;
    }
    while (left) {
      if (part == count)
        goto complete;
      uint64_t in = offset - base, n = segments[part].length - in;
      if (n > left)
        n = left;
      memcpy((uint8_t *)ng_ram.address + segments[part].physical -
                 UINT64_C(0x40000000) + in,
             from, (size_t)n);
      from += n;
      left -= n;
      offset += n;
      if (in + n == segments[part].length) {
        base += segments[part].length;
        part++;
      }
    }
  }
  status = 0;
complete:
  {
    int32_t *cell = (int32_t *)((uint8_t *)ng_ram.address + completion -
                                UINT64_C(0x40000000));
    __atomic_store_n(cell, status, __ATOMIC_RELAXED);
    __atomic_store_n((uint32_t *)(cell + 1), (uint32_t)sequence,
                     __ATOMIC_RELEASE);
  }
  result = ng_output(output, capacity, used, &status, 4);
invalid_cell:
  free(pixels);
  free(sorted);
  free(segments);
  return result;
}
