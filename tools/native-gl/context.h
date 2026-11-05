/* Explicit GLES context lifetime for the future EAGL adapter. */
#ifndef IOS_KVM_NATIVE_GL_CONTEXT_H
#define IOS_KVM_NATIVE_GL_CONTEXT_H
struct ios_kvm_native_gl_context;
/* Currently GLES3 only. Unsupported versions or failed handshakes return NULL.
 */
struct ios_kvm_native_gl_context *ios_kvm_native_gl_context_create(int version);
/* NULL releases this thread's context. A context current elsewhere returns0. */
int ios_kvm_native_gl_make_current(struct ios_kvm_native_gl_context *context);
/* EAGL macro calls borrow one context at a time across worker threads. The
 * waiting variant releases a different previous context before waiting. */
int ios_kvm_native_gl_make_current_wait(struct ios_kvm_native_gl_context *context);
/* Release this thread's explicit binding without flushing its owned queue.
 * The owner keeps the context alive until its next call or destruction. */
int ios_kvm_native_gl_release_deferred(void);
/* Caller synchronizes destruction with future bindings; returns0 while
 * the context is current elsewhere. */
int ios_kvm_native_gl_context_destroy(
    struct ios_kvm_native_gl_context *context);
void ios_kvm_native_gl_disconnect(void);
#endif
