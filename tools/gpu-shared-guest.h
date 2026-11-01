/* ios-kvm experimental shared-RAM readback. Included by Mesa's vtest transport. */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include "gpu-shared-request.h"
#include "gpu-async-policy.h"

struct ios_kvm_pin {
   io_connect_t connection;
   void *destination;
   uint32_t bytes, count, surface;
   size_t length;
   struct ios_kvm_shared_request request;
};

/* An explicit owner keeps the allocation alive until lease_destroy; no address-based caching. */
static __thread struct ios_kvm_pin *ios_kvm_active_readback;
__attribute__((visibility("default"))) void *ios_kvm_readback_lease_create(void *, uint32_t);
__attribute__((visibility("default"))) void *ios_kvm_readback_lease_set(void *);
__attribute__((visibility("default"))) void ios_kvm_readback_lease_destroy(void *);
__attribute__((visibility("default"))) void ios_kvm_readback_lease_surface(void *, uint32_t);

__attribute__((visibility("default"))) void *ios_kvm_readback_lease_create(void *destination, uint32_t bytes)
{
   const char *enabled = getenv("IOS_KVM_SHARED");
   if (!enabled || strcmp(enabled, "1") || bytes < 65536 || bytes > (64u << 20)) return NULL;
   struct ios_kvm_pin *pin = calloc(1, sizeof(*pin));
   if (!pin) return NULL;
   io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceNameMatching("IOCoreSurfaceRoot"));
   kern_return_t status = service ? IOServiceOpen(service, mach_task_self(), 0, &pin->connection) : KERN_FAILURE;
   if (service) IOObjectRelease(service);
   if (status) { free(pin); return NULL; }
   uint64_t input[2] = {(uintptr_t)destination, bytes};
   pin->length = sizeof(pin->request.segments);
   status = IOConnectCallMethod(pin->connection, 0x100, input, 2, NULL, 0, NULL, NULL, pin->request.segments, &pin->length);
   if (status) { IOServiceClose(pin->connection); free(pin); return NULL; }
   pin->destination = destination;
   pin->bytes = bytes;
   pin->count = pin->length / 16;
   return pin;
}

__attribute__((visibility("default"))) void *ios_kvm_readback_lease_set(void *lease)
{
   void *previous = ios_kvm_active_readback;
   ios_kvm_active_readback = lease;
   return previous;
}

/* Only the EAGL IOSurface owner sets this; ordinary glReadPixels stays synchronous. */
__attribute__((visibility("default"))) void ios_kvm_readback_lease_surface(void *lease, uint32_t surface)
{
   if (lease) ((struct ios_kvm_pin *)lease)->surface = surface;
}

__attribute__((visibility("default"))) void ios_kvm_readback_lease_destroy(void *lease)
{
   struct ios_kvm_pin *pin = lease;
   if (!pin) return;
   if (ios_kvm_active_readback == pin) ios_kvm_active_readback = NULL;
   /* Tagged async surfaces have a separate kernel hold through completion. */
   IOServiceClose(pin->connection); /* release this lease's owning-task pin */
   free(pin);
}

static int ios_kvm_submit_pin_command(struct virgl_vtest_winsys *vws, uint32_t handle,
                             uint32_t level, uint32_t stride, uint32_t layer_stride,
                             const struct pipe_box *box, struct ios_kvm_pin *pin, uint32_t command)
{
   uint32_t args[12] = {handle, level, stride, layer_stride,
      box->x, box->y, box->z, box->width, box->height, box->depth, pin->bytes, pin->count};
   size_t size = ios_kvm_prepare_shared_request(&pin->request, args, pin->length);
   if (!size) return -1;
   pin->request.words[1] = command;
   const char *batch = getenv("IOS_KVM_SHARED_BATCH");
   int ret;
   if (!batch || strcmp(batch, "1")) {
      ret = virgl_block_write(vws->sock_fd, pin->request.words, 8);
      if (ret > 0) ret = virgl_block_write(vws->sock_fd, pin->request.words + 2, sizeof(args));
      if (ret > 0) ret = virgl_block_write(vws->sock_fd, pin->request.segments, pin->length);
   } else {
      ret = virgl_block_write(vws->sock_fd, pin->request.words, size);
   }
   int32_t reply = -1;
   if (ret > 0) ret = virgl_block_read(vws->sock_fd, &reply, sizeof(reply));
   return ret == sizeof(reply) && reply == 0 ? 1 : -1;
}

static int ios_kvm_submit_pin_async(struct virgl_vtest_winsys *vws, uint32_t handle,
                                  uint32_t stride, const struct pipe_box *box, struct ios_kvm_pin *pin)
{
   if (!pin->surface || !(vws->ios_kvm_features & IOS_KVM_FEATURE_ASYNC_READBACK) ||
       !ios_kvm_async_requested()) return 0;
   uint32_t args[12] = {handle, 0, stride, 0, box->x, box->y, box->z,
                       box->width, box->height, box->depth, pin->bytes, pin->count};
   /* Validate the wire buffer before taking the irrevocable kernel hold. */
   if (!ios_kvm_prepare_shared_request(&pin->request, args, pin->length)) return -1;
   uint64_t input[2] = {pin->surface, pin->bytes}, output[2] = {0}; uint32_t count = 2;
   kern_return_t status = IOConnectCallScalarMethod(pin->connection, 0x102, input, 2, output, &count);
   if (status != KERN_SUCCESS) return 0; /* Old/disabled kernel or ineligible surface: sync fallback. */
   if (count != 2 || output[1] > UINT32_MAX) return -1;
   size_t size = ios_kvm_prepare_async_request(&pin->request, args, pin->length, output[0], output[1]);
   /* After preparation, failure must retain the backing: a host may already be
    * writing. No cancellation or synchronous fallback may recycle these pages. */
   int result = size && virgl_block_write(vws->sock_fd, pin->request.words, size) == (int)size ? 1 : -1;
   static unsigned submissions;
   if (++submissions == 1 || result < 0)
      fprintf(stderr, "ios-kvm async readback: surface=%u bytes=%u sequence=%u result=%d\n",
              pin->surface, pin->bytes, (unsigned)output[1], result);
   return result;
}

static int ios_kvm_submit_pin(struct virgl_vtest_winsys *vws, uint32_t handle,
                             uint32_t level, uint32_t stride, uint32_t layer_stride,
                             const struct pipe_box *box, struct ios_kvm_pin *pin)
{
   return ios_kvm_submit_pin_command(vws, handle, level, stride, layer_stride, box, pin, 64);
}

void virgl_vtest_direct_release(struct virgl_vtest_winsys *vws)
{
   struct ios_kvm_pin *pin = vws->ios_kvm_direct_pin;
   if (pin) { IOServiceClose(pin->connection); free(pin); }
   vws->ios_kvm_direct_pin = NULL;
}

int virgl_vtest_direct_get(struct virgl_vtest_winsys *vws, uint32_t handle,
                          const struct pipe_box *box, uint32_t stride, uint32_t bytes, void *pixels)
{
   const char *enabled = getenv("IOS_KVM_SHARED");
   if (!enabled || strcmp(enabled, "1")) return 0;
   struct ios_kvm_pin *lease = ios_kvm_active_readback;
   if (lease && lease->destination == pixels && lease->bytes == bytes) {
      int asynchronous = ios_kvm_submit_pin_async(vws, handle, stride, box, lease);
      return asynchronous ? asynchronous : ios_kvm_submit_pin(vws, handle, 0, stride, 0, box, lease);
   }
   struct ios_kvm_pin *pin = vws->ios_kvm_direct_pin;
   if (!pin) {
      pin = calloc(1, sizeof(*pin));
      if (!pin) return 0;
      io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault,
         IOServiceNameMatching("IOCoreSurfaceRoot"));
      kern_return_t status = service ? IOServiceOpen(service, mach_task_self(), 0, &pin->connection) : KERN_FAILURE;
      if (service) IOObjectRelease(service);
      if (status) { free(pin); return 0; }
      vws->ios_kvm_direct_pin = pin;
   }
   uint64_t input[2] = {(uintptr_t)pixels, bytes};
   pin->length = sizeof(pin->request.segments);
   kern_return_t status = IOConnectCallMethod(pin->connection, 0x100, input, 2, NULL, 0,
                                             NULL, NULL, pin->request.segments, &pin->length);
   if (status) return 0;
   pin->bytes = bytes;
   pin->count = pin->length / 16;
   int result = ios_kvm_submit_pin(vws, handle, 0, stride, 0, box, pin);
   /* No caller allocation may remain pinned past this synchronous readback. */
   status = IOConnectCallMethod(pin->connection, 0x101, NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
   pin->count = pin->bytes = 0;
   if (status) { virgl_vtest_direct_release(vws); return -1; }
   return result;
}

void virgl_vtest_shared_release(struct virgl_hw_res *resource)
{
   struct ios_kvm_pin *pin = resource->ios_kvm_pin;
   if (pin) {
      IOServiceClose(pin->connection);
      free(pin);
      resource->ios_kvm_pin = NULL;
   }
}

static int ios_kvm_shared_transfer(struct virgl_vtest_winsys *vws,
                          uint32_t handle, uint32_t level, uint32_t stride,
                          uint32_t layer_stride, const struct pipe_box *box,
                          uint32_t bytes, void *destination, struct virgl_hw_res *resource, uint32_t command)
{
   const char *enabled = getenv("IOS_KVM_SHARED");
   if (!enabled || strcmp(enabled, "1") || bytes < 65536 || bytes > (64u << 20))
      return 0;
   struct ios_kvm_pin *pin = resource->ios_kvm_pin;
   if (pin && (pin->destination != destination || pin->bytes != bytes)) {
      virgl_vtest_shared_release(resource);
      pin = NULL;
   }
   if (!pin) {
      pin = calloc(1, sizeof(*pin));
      if (!pin) return -1;
      io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault,
         IOServiceNameMatching("IOCoreSurfaceRoot"));
      kern_return_t status = service ? IOServiceOpen(service, mach_task_self(), 0, &pin->connection) : KERN_FAILURE;
      if (service) IOObjectRelease(service);
      if (status) { free(pin); return 0; }
      uint64_t input[2] = {(uintptr_t)destination, bytes};
      pin->length = sizeof(pin->request.segments);
      status = IOConnectCallMethod(pin->connection, 0x100, input, 2, NULL, 0,
                                  NULL, NULL, pin->request.segments, &pin->length);
      if (status) { IOServiceClose(pin->connection); free(pin); return 0; }
      pin->destination = destination;
      pin->bytes = bytes;
      pin->count = pin->length / 16;
      resource->ios_kvm_pin = pin;
   }
   /* The host acknowledges completion before caller RAM can be reused. */
   /* The pin is reused with this resource, and released before its memory is freed. */
   return ios_kvm_submit_pin_command(vws, handle, level, stride, layer_stride, box, pin, command);
}

int virgl_vtest_shared_get(struct virgl_vtest_winsys *vws,
                          uint32_t handle, uint32_t level, uint32_t stride,
                          uint32_t layer_stride, const struct pipe_box *box,
                          uint32_t bytes, void *destination, struct virgl_hw_res *resource)
{
   return ios_kvm_shared_transfer(vws, handle, level, stride, layer_stride, box,
                                 bytes, destination, resource, 64);
}

int virgl_vtest_shared_upload_enabled(struct virgl_vtest_winsys *vws)
{
   return ios_kvm_upload_policy(vws->ios_kvm_features, getenv("IOS_KVM_SHARED_UPLOAD"));
}

int virgl_vtest_shared_put(struct virgl_vtest_winsys *vws,
                          uint32_t handle, uint32_t level, uint32_t stride,
                          uint32_t layer_stride, const struct pipe_box *box,
                          uint32_t bytes, void *source, struct virgl_hw_res *resource)
{
   if (!virgl_vtest_shared_upload_enabled(vws) || bytes < IOS_KVM_SHARED_UPLOAD_MIN_BYTES) return 0;
   /* Pin only for this synchronous upload: staging caches must not keep
    * arbitrary source allocations wired after the host has consumed them. */
   (void)resource;
   struct virgl_hw_res temporary = {0};
   int result = ios_kvm_shared_transfer(vws, handle, level, stride, layer_stride, box,
                                      bytes, source, &temporary, 65);
   virgl_vtest_shared_release(&temporary);
   return result;
}
