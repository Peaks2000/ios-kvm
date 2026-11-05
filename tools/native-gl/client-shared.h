#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdatomic.h>
struct ng_pin {
  io_connect_t connection;
  void *destination;
  uint32_t bytes, count, surface;
  uint64_t segments[8192];
};
static _Thread_local struct ng_pin *ng_active_pin;
static _Atomic uint64_t ng_shared_submissions;
static _Atomic uint64_t ng_shared_async_submissions;
uint64_t ios_kvm_native_gl_shared_submissions(void) {
  return atomic_load(&ng_shared_submissions);
}
uint64_t ios_kvm_native_gl_async_submissions(void) {
  return atomic_load(&ng_shared_async_submissions);
}
static int ng_shared_enabled(void) {
  const char *mode = getenv("IOS_KVM_NATIVE_SHARED");
  return mode && !strcmp(mode, "1");
}
void *ios_kvm_native_gl_readback_pin(void *destination, uint32_t bytes) {
  if (!ng_shared_enabled() || !destination || bytes < 65536 ||
      bytes > (64u << 20))
    return NULL;
  struct ng_pin *p = calloc(1, sizeof(*p));
  if (!p)
    return NULL;
  io_service_t service = IOServiceGetMatchingService(
      kIOMainPortDefault, IOServiceNameMatching("IOCoreSurfaceRoot"));
  kern_return_t status =
      service ? IOServiceOpen(service, mach_task_self(), 0, &p->connection)
              : KERN_FAILURE;
  if (service)
    IOObjectRelease(service);
  if (status) {
    free(p);
    return NULL;
  }
  uint64_t input[] = {(uintptr_t)destination, bytes};
  size_t length = sizeof(p->segments);
  status = IOConnectCallMethod(p->connection, 0x100, input, 2, NULL, 0, NULL,
                               NULL, p->segments, &length);
  if (status || !length || length % 16 || length > sizeof(p->segments)) {
    IOServiceClose(p->connection);
    free(p);
    return NULL;
  }
  p->destination = destination;
  p->bytes = bytes;
  p->count = length / 16;
  return p;
}
void ios_kvm_native_gl_readback_release(void *lease) {
  struct ng_pin *p = lease;
  if (!p)
    return;
  if (ng_active_pin == p)
    ng_active_pin = NULL;
  IOServiceClose(p->connection);
  free(p);
}
void *ios_kvm_native_gl_readback_set(void *lease) {
  void *previous = ng_active_pin;
  ng_active_pin = lease;
  return previous;
}
void ios_kvm_native_gl_readback_surface(void *lease, uint32_t surface) {
  if (lease)
    ((struct ng_pin *)lease)->surface = surface;
}
static int ng_shared_try_read(GLint x, GLint y, GLsizei w, GLsizei h,
                              GLenum format, GLenum type, void *destination) {
  struct ng_pin *p = ng_active_pin;
  if (!p || !p->surface || p->destination != destination ||
      !ng_shared_enabled() ||
      !(ng_current()->features & NG_FEATURE_SHARED_READ))
    return 0;
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096 ||
      (format != GL_RGBA && format != NG_BGRA) || type != GL_UNSIGNED_BYTE)
    return 0;
  size_t stride = 0, skip = 0;
  ng_pixel_layout(1, w, h, format, type, &stride, &skip);
  if (skip || stride < (size_t)w * 4 || stride > (64u << 20) || (stride & 3) ||
      stride * (size_t)(h - 1) + (size_t)w * 4 != p->bytes)
    return 0;
  size_t length = 11 * 8 + (size_t)p->count * 16;
  uint8_t *request = malloc(length);
  if (!request)
    return 0;
  uint64_t a[11] = {
      (uint64_t)x, (uint64_t)y, (uint64_t)w, (uint64_t)h, format, type,
      stride,      p->bytes,    p->count,    0,           0};
  memcpy(request, a, sizeof(a));
  memcpy(request + sizeof(a), p->segments, (size_t)p->count * 16);
  uint64_t input[] = {p->surface, p->bytes}, out[2] = {0};
  uint32_t count = 2;
  kern_return_t status =
      IOConnectCallScalarMethod(p->connection, 0x102, input, 2, out, &count);
  if (status) {
    free(request);
    return 0;
  } /* Fallback is allowed only before a hold exists. */
  if (count != 2 || !out[0] || (out[0] & 7) || !out[1] || out[1] > UINT32_MAX)
    ng_failure("invalid retained-surface completion");
  a[9] = out[0];
  a[10] = out[1];
  memcpy(request, a, sizeof(a));
  atomic_fetch_add(&ng_shared_submissions, 1);
  const char *async_mode = getenv("IOS_KVM_NATIVE_ASYNC_READBACK");
  if (async_mode && !strcmp(async_mode, "1")) {
    ng_enqueue(NG_READ_SHARED, request, length);
    ng_submit_context(ng_current(), NULL, 4, 1);
    uint64_t previous = atomic_fetch_add(&ng_shared_async_submissions, 1);
    const char *trace = getenv("IOS_KVM_NATIVE_TRACE_ERRORS");
    if (!previous && trace && !strcmp(trace, "1"))
      fprintf(stderr, "native GLES asynchronous retained submission active\n");
    free(request);
    return 1;
  }
  int32_t completion = -1;
  ng_call(NG_READ_SHARED, request, length, &completion, 4);
  free(request);
  /* Never copy/fallback or cancel after the kernel hold, even on host failure.
   */
  if (completion)
    fprintf(stderr, "ios-kvm native shared readback failed: %d\n", completion);
  return 1;
}
