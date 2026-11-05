/* Linked only into the temporary compositor experiment's injection library. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifndef IOS_KVM_NATIVE_TOKEN_FILE
#error "the experiment needs its private guest token path"
#endif
#ifndef IOS_KVM_NATIVE_PORT
#error "the experiment needs its host port"
#endif
#ifndef IOS_KVM_NATIVE_ASYNC_MODE
#define IOS_KVM_NATIVE_ASYNC_MODE "0"
#endif
#ifndef IOS_KVM_NATIVE_SHARED_MODE
#define IOS_KVM_NATIVE_SHARED_MODE "1"
#endif
#ifndef IOS_KVM_NATIVE_RENDER_STATE_CACHE
#define IOS_KVM_NATIVE_RENDER_STATE_CACHE "1"
#endif
int ios_kvm_native_gl_environment(void) {
  if (geteuid() != 501) {
    fprintf(stderr, "ios-kvm: native compositor experiment requires UID501\n");
    return 0;
  }
  if (setenv("IOS_KVM_NATIVE_GL_HOST", "10.0.2.2", 1) ||
      setenv("IOS_KVM_NATIVE_GL_PORT", IOS_KVM_NATIVE_PORT, 1) ||
      setenv("IOS_KVM_NATIVE_GL_TOKEN_FILE", IOS_KVM_NATIVE_TOKEN_FILE, 1) ||
      setenv("IOS_KVM_NATIVE_SHARED", IOS_KVM_NATIVE_SHARED_MODE, 1) ||
      setenv("IOS_KVM_NATIVE_ASYNC_READBACK", IOS_KVM_NATIVE_ASYNC_MODE, 1) ||
      setenv("IOS_KVM_NATIVE_RENDER_STATE_CACHE", IOS_KVM_NATIVE_RENDER_STATE_CACHE, 1) ||
      setenv("IOS_KVM_NATIVE_TRACE_ERRORS", "1", 1) ||
      setenv("IOS_KVM_NATIVE_GL_CHECK_EVERY_CALL", "0", 1)) {
    perror("ios-kvm native compositor environment");
    return 0;
  }
  fprintf(stderr, "ios-kvm: temporary native compositor experiment UID501\n");
  return 1;
}
