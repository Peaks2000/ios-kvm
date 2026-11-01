/* Destroy an owned context as soon as a worker releases it during TLS teardown.
 */
#define _POSIX_C_SOURCE 200809L
#include "context.h"
#include <GLES3/gl3.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

struct handoff {
  struct ios_kvm_native_gl_context *context;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  int ready;
};
static void check(int condition) {
  if (!condition)
    abort();
}
static void *worker(void *argument) {
  struct handoff *h = argument;
  check(ios_kvm_native_gl_make_current(h->context));
  glClearColor(.25f, .5f, .75f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  pthread_mutex_lock(&h->mutex);
  h->ready = 1;
  pthread_cond_signal(&h->condition);
  pthread_mutex_unlock(&h->mutex);
  return NULL; /* TLS destructor flushes queued commands, then releases
                  ownership. */
}
int main(void) {
  for (unsigned i = 0; i < 32; i++) {
    struct handoff h = {.context = ios_kvm_native_gl_context_create(3),
                        .mutex = PTHREAD_MUTEX_INITIALIZER,
                        .condition = PTHREAD_COND_INITIALIZER};
    check(h.context != NULL);
    pthread_t thread;
    check(!pthread_create(&thread, NULL, worker, &h));
    pthread_mutex_lock(&h.mutex);
    while (!h.ready)
      pthread_cond_wait(&h.condition, &h.mutex);
    pthread_mutex_unlock(&h.mutex);
    while (!ios_kvm_native_gl_context_destroy(h.context))
      sched_yield();
    pthread_join(thread, NULL);
    pthread_cond_destroy(&h.condition);
    pthread_mutex_destroy(&h.mutex);
  }
  puts("Native GL context teardown race check PASSED (32 worker-exit/destroy "
       "handoffs)");
  return 0;
}
