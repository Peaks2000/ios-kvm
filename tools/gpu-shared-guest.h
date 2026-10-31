/* ios-kvm experimental shared-RAM readback. Included by Mesa's vtest transport. */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>

struct ios_kvm_pin {
   io_connect_t connection;
   void *destination;
   uint32_t bytes, count;
   size_t length;
   uint64_t segments[8192];
};

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
      pin->length = sizeof(pin->segments);
      status = IOConnectCallMethod(pin->connection, 0x100, input, 2, NULL, 0,
                                  NULL, NULL, pin->segments, &pin->length);
      if (status) { IOServiceClose(pin->connection); free(pin); return 0; }
      pin->destination = destination;
      pin->bytes = bytes;
      pin->count = pin->length / 16;
      resource->ios_kvm_pin = pin;
   }
   uint32_t count = pin->count;
   uint32_t header[2] = {12 + count * 4, 64}; /* ios-kvm private vtest command */
   uint32_t args[12] = {handle, level, stride, layer_stride,
      box->x, box->y, box->z, box->width, box->height, box->depth, bytes, count};
   int ret = virgl_block_write(vws->sock_fd, header, sizeof(header));
   if (ret > 0) ret = virgl_block_write(vws->sock_fd, args, sizeof(args));
   if (ret > 0) ret = virgl_block_write(vws->sock_fd, pin->segments, pin->length);
   int32_t reply = -1;
   if (ret > 0) ret = virgl_block_read(vws->sock_fd, &reply, sizeof(reply));
   /* Host acknowledgement is sent only after every write to pinned RAM finishes. */
   /* The pin is reused with this resource, and released before its memory is freed. */
   return ret == sizeof(reply) && reply == 0 ? 1 : -1;
}
