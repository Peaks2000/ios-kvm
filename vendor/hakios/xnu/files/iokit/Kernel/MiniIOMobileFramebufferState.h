#ifndef IOS_KVM_DISPLAY_STATE_H
#define IOS_KVM_DISPLAY_STATE_H
#include <stdint.h>
#include "MiniIOSurfaceFrame.h"

/* Read-only kern.ios_kvm_display_state, describing the last completed request.
 * Source/destination rectangles retain the framebuffer protocol's coordinates. */
#define IOS_KVM_DISPLAY_STATE_VERSION 1
struct ios_kvm_display_layer {
    uint32_t surface, enabled;
    int32_t source[4], destination[4];
};
struct ios_kvm_display_state {
    uint32_t version, size;
    uint64_t sequence;
    uint32_t swap_id, layers_set, layers_cleared;
    uint32_t width, height, panel_width, panel_height, row_bytes;
    struct ios_kvm_display_layer layers[4];
};
#define IOS_KVM_DISPLAY_FRAME_VERSION 1
struct ios_kvm_display_frame {
    uint32_t version, size;
    struct ios_kvm_display_state presentation;
    struct ios_kvm_surface_frame_key keys[4];
};
#ifdef __cplusplus
static_assert(sizeof(struct ios_kvm_display_state) == 208, "display state ABI");
static_assert(sizeof(struct ios_kvm_display_frame) == 312, "display frame ABI");
#else
_Static_assert(sizeof(struct ios_kvm_display_state) == 208, "display state ABI");
_Static_assert(sizeof(struct ios_kvm_display_frame) == 312, "display frame ABI");
#endif
#endif
