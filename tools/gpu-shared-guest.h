/* ios-kvm experimental shared-RAM readback. Included by Mesa's vtest transport. */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include "gpu-shared-request.h"

struct ios_kvm_pin {
   io_connect_t connection;
   void *destination;
   uint32_t bytes, count;
   size_t length;
   struct ios_kvm_shared_request request;
};

/* An explicit owner keeps the allocation alive until lease_destroy; no address-based caching. */
static __thread struct ios_kvm_pin *ios_kvm_active_readback;
__attribute__((visibility("default"))) void *ios_kvm_readback_lease_create(void *, uint32_t);
__attribute__((visibility("default"))) void *ios_kvm_readback_lease_set(void *);
__attribute__((visibility("default"))) void ios_kvm_readback_lease_destroy(void *);

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

__attribute__((visibility("default"))) void ios_kvm_readback_lease_destroy(void *lease)
{
   struct ios_kvm_pin *pin = lease;
   if (!pin) return;
   if (ios_kvm_active_readback == pin) ios_kvm_active_readback = NULL;
   IOServiceClose(pin->connection); /* releases the pin before its owner frees the allocation */
   free(pin);
}

static int ios_kvm_submit_pin(struct virgl_vtest_winsys *vws, uint32_t handle,
                             uint32_t level, uint32_t stride, uint32_t layer_stride,
                             const struct pipe_box *box, struct ios_kvm_pin *pin)
{
   uint32_t args[12] = {handle, level, stride, layer_stride,
      box->x, box->y, box->z, box->width, box->height, box->depth, pin->bytes, pin->count};
   size_t size = ios_kvm_prepare_shared_request(&pin->request, args, pin->length);
   if (!size) return -1;
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
   if (lease && lease->destination == pixels && lease->bytes == bytes)
      return ios_kvm_submit_pin(vws, handle, 0, stride, 0, box, lease);
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

int virgl_vtest_shared_get(struct virgl_vtest_winsys *vws,
                          uint32_t handle, uint32_t level, uint32_t stride,
                          uint32_t layer_stride, const struct pipe_box *box,
                          uint32_t bytes, void *destination, struct virgl_hw_res *resource)
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
   /* Host acknowledgement is sent only after every write to pinned RAM finishes. */
   /* The pin is reused with this resource, and released before its memory is freed. */
   return ios_kvm_submit_pin(vws, handle, level, stride, layer_stride, box, pin);
}
