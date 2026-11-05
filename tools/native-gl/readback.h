#ifndef IOS_KVM_NATIVE_GL_READBACK_H
#define IOS_KVM_NATIVE_GL_READBACK_H
#include <stdint.h>
#include "../../vendor/hakios/xnu/files/iokit/Kernel/MiniIOSurfaceFrame.h"
/* Active lease bindings are thread local. The caller keeps the IOSurface alive,
 * restores any previous active lease, and releases before dropping the surface.
 * Only a matching kernel-backed surface tag permits the host-retained path.
 * IOS_KVM_NATIVE_ASYNC_READBACK=1 permits a tagged shared read to return after
 * sending commands. The caller must use IOSurface locking/completion before
 * reading or reusing its storage. Untagged reads retain synchronous semantics.
 */
void *ios_kvm_native_gl_readback_pin(void *address, uint32_t bytes);
void ios_kvm_native_gl_readback_release(void *lease);
void *ios_kvm_native_gl_readback_set(void *lease);
void ios_kvm_native_gl_readback_surface(void *lease, uint32_t surface);
/* Query current CPU-write eligibility without creating a host hold. Older
 * kernels and untagged/ineligible surfaces return0 with a zero key. */
int ios_kvm_native_gl_readback_key(void *lease, struct ios_kvm_surface_frame_key *key);
uint64_t ios_kvm_native_gl_shared_submissions(void);
uint64_t ios_kvm_native_gl_async_submissions(void);
#endif
