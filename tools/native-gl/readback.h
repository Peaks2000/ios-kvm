#ifndef IOS_KVM_NATIVE_GL_READBACK_H
#define IOS_KVM_NATIVE_GL_READBACK_H
#include <stdint.h>
/* Active lease bindings are thread local. The caller keeps the IOSurface alive,
 * restores any previous active lease, and releases before dropping the surface.
 * Only a matching kernel-backed surface tag permits the host-retained path.
 * All reads currently wait for an RPC acknowledgement, including shared reads.
 */
void *ios_kvm_native_gl_readback_pin(void *address, uint32_t bytes);
void ios_kvm_native_gl_readback_release(void *lease);
void *ios_kvm_native_gl_readback_set(void *lease);
void ios_kvm_native_gl_readback_surface(void *lease, uint32_t surface);
uint64_t ios_kvm_native_gl_shared_submissions(void);
#endif
