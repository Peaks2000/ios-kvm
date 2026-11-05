#ifndef IOS_KVM_SURFACE_FRAME_H
#define IOS_KVM_SURFACE_FRAME_H
#include <stdint.h>

/* A surface-lifetime epoch and one submitted GPU generation within that life.
 * Zero denotes unavailable. Epochs and sequences never wrap within a VM. */
struct ios_kvm_surface_frame_key {
    uint64_t epoch, cell;
    uint32_t sequence, reserved;
};
#ifdef __cplusplus
static_assert(sizeof(struct ios_kvm_surface_frame_key) == 24, "surface frame key ABI");
#else
_Static_assert(sizeof(struct ios_kvm_surface_frame_key) == 24, "surface frame key ABI");
#endif
#endif
