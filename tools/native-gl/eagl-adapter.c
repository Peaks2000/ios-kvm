/* Context adapter for build-time EAGL integration work; not installed at boot.
 */
#include "context.h"
#include "hakios.gl.h"
#include "readback.h"
#include <stdint.h>
#include <stdio.h>

struct hakios_gl *hakios_gl_create(int version) {
  return (struct hakios_gl *)ios_kvm_native_gl_context_create(version);
}
int hakios_gl_make_current(struct hakios_gl *context) {
  return ios_kvm_native_gl_make_current(
      (struct ios_kvm_native_gl_context *)context);
}
void hakios_gl_destroy(struct hakios_gl *context) {
  if (!ios_kvm_native_gl_context_destroy(
          (struct ios_kvm_native_gl_context *)context))
    fprintf(stderr,
            "ios-kvm native GLES: context remains current on another thread\n");
}
const char *hakios_gl_library(void) {
  return "ios-kvm native GLES (experimental)";
}
/* Only explicit kernel-retained surface leases enable shared readback. */
void *hakios_gl_readback_pin(void *address, uint32_t bytes) {
  return ios_kvm_native_gl_readback_pin(address, bytes);
}
void hakios_gl_readback_release(void *lease) {
  ios_kvm_native_gl_readback_release(lease);
}
void hakios_gl_readback_surface(void *lease, uint32_t surface) {
  ios_kvm_native_gl_readback_surface(lease, surface);
}
void *hakios_gl_readback_set(void *lease) {
  return ios_kvm_native_gl_readback_set(lease);
}
int hakios_gl_surface_texture(struct hakios_gl *context, uint32_t surface) {
  (void)context;
  (void)surface;
  return 0;
}
