/* Experimental native GLES proxy. Not installed into EAGL by the launcher. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include "context.h"
#include "io.h"
#include "protocol.h"
#include "pixel-format.h"
#include "readback.h"
#include "schema.h"
#include <GLES3/gl31.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct ng_string_entry {
  GLenum pname;
  GLuint index;
  int indexed;
  char *text;
};
struct ng_pixel_store {
  GLint alignment, row, rows, pixels, buffer;
  int buffer_known;
};
struct ios_kvm_native_gl_context {
  int fd;
  int explicit_owner;
  pthread_mutex_t ownership;
  uint32_t sequence, features;
  size_t used, allocated;
  uint8_t *commands;
  struct ng_string_entry *strings;
  unsigned string_count, string_capacity;
  struct ng_pixel_store pixel_store[2]; /* unpack, pack */
};
static pthread_key_t ng_key;
static pthread_once_t ng_once = PTHREAD_ONCE_INIT;
static void ng_failure(const char *reason) {
  fprintf(stderr, "ios-kvm native GL: %s\n", reason);
  abort();
}
static size_t ng_flush_context(struct ios_kvm_native_gl_context *, void *,
                               size_t);
static void ng_free(struct ios_kvm_native_gl_context *c) {
  if (!c)
    return;
  if (c->fd >= 0)
    close(c->fd);
  free(c->commands);
  for (unsigned i = 0; i < c->string_count; i++)
    free(c->strings[i].text);
  free(c->strings);
  pthread_mutex_destroy(&c->ownership);
  free(c);
}
static void ng_tls_release(void *pointer) {
  struct ios_kvm_native_gl_context *c = pointer;
  if (!c)
    return;
  if (c->used)
    ng_flush_context(c, NULL, 0);
  int implicit = !c->explicit_owner;
  pthread_mutex_unlock(&c->ownership);
  if (implicit)
    ng_free(c);
}
static void ng_key_init(void) {
  if (pthread_key_create(&ng_key, ng_tls_release))
    ng_failure("TLS key");
}
static struct ios_kvm_native_gl_context *ng_open(int explicit_owner) {
  struct ios_kvm_native_gl_context *c = calloc(1, sizeof(*c));
  if (!c)
    return NULL;
  c->fd = -1;
  if (pthread_mutex_init(&c->ownership, NULL)) {
    free(c);
    return NULL;
  }
  c->explicit_owner = explicit_owner;
  struct ng_hello hello = {
      .magic = NG_MAGIC, .version = NG_VERSION, .schema = NG_SCHEMA};
  if (ng_token(getenv("IOS_KVM_NATIVE_GL_TOKEN_FILE"), hello.token))
    goto failed;
  const char *host = getenv("IOS_KVM_NATIVE_GL_HOST"),
             *port = getenv("IOS_KVM_NATIVE_GL_PORT");
  char *end = NULL;
  unsigned long number = port ? strtoul(port, &end, 10) : 6733;
  if (number < 1024 || number > 65535 || (port && (!*port || *end)))
    goto failed;
  struct sockaddr_in address = {.sin_family = AF_INET,
                                .sin_port = htons(number)};
  if (inet_pton(AF_INET, host ? host : "10.0.2.2", &address.sin_addr) != 1)
    goto failed;
  c->fd = socket(AF_INET, SOCK_STREAM, 0);
  if (c->fd < 0)
    goto failed;
  ng_socket_options(c->fd);
  if (connect(c->fd, (struct sockaddr *)&address, sizeof(address)))
    goto failed;
  struct ng_ack ack = {0};
  if (ng_io(c->fd, &hello, sizeof(hello), 1) ||
      ng_io(c->fd, &ack, sizeof(ack), 0) || ack.magic != NG_MAGIC ||
      (ack.features & ~NG_FEATURE_SHARED_READ))
    goto failed;
  c->features = ack.features;
  for (unsigned i = 0; i < 2; i++) {
    c->pixel_store[i].alignment = 4;
    c->pixel_store[i].buffer_known = 1;
  }
  return c;
failed:
  ng_free(c);
  return NULL;
}
struct ios_kvm_native_gl_context *
ios_kvm_native_gl_context_create(int version) {
  if (version != 3)
    return NULL;
  return ng_open(1);
}
int ios_kvm_native_gl_make_current(struct ios_kvm_native_gl_context *next) {
  pthread_once(&ng_once, ng_key_init);
  struct ios_kvm_native_gl_context *old = pthread_getspecific(ng_key);
  if (old == next)
    return 1;
  if (next && pthread_mutex_trylock(&next->ownership))
    return 0;
  if (old && old->used)
    ng_flush_context(old, NULL, 0);
  if (pthread_setspecific(ng_key, next)) {
    if (next)
      pthread_mutex_unlock(&next->ownership);
    return 0;
  }
  if (old) {
    int implicit = !old->explicit_owner;
    pthread_mutex_unlock(&old->ownership);
    if (implicit)
      ng_free(old);
  }
  return 1;
}
int ios_kvm_native_gl_context_destroy(struct ios_kvm_native_gl_context *c) {
  if (!c)
    return 1;
  pthread_once(&ng_once, ng_key_init);
  if (pthread_getspecific(ng_key) == c) {
    if (!ios_kvm_native_gl_make_current(NULL))
      return 0;
  }
  if (pthread_mutex_trylock(&c->ownership))
    return 0;
  pthread_mutex_unlock(&c->ownership);
  ng_free(c);
  return 1;
}
static struct ios_kvm_native_gl_context *ng_current(void) {
  pthread_once(&ng_once, ng_key_init);
  struct ios_kvm_native_gl_context *c = pthread_getspecific(ng_key);
  if (c)
    return c;
  c = ng_open(0);
  if (!c)
    ng_failure("implicit context connection");
  if (!ios_kvm_native_gl_make_current(c)) {
    ng_free(c);
    ng_failure("implicit context ownership");
  }
  return c;
}
static size_t ng_flush_context(struct ios_kvm_native_gl_context *c,
                               void *output, size_t capacity) {
  if (!c->used) {
    if (capacity)
      ng_failure("empty query");
    return 0;
  }
  if (capacity > NG_LIMIT || c->sequence == UINT32_MAX)
    ng_failure("request limit");
  struct ng_batch batch = {NG_MAGIC, ++c->sequence, (uint32_t)c->used,
                           (uint32_t)capacity};
  struct ng_reply reply;
  if (ng_io(c->fd, &batch, sizeof(batch), 1) ||
      ng_io(c->fd, c->commands, c->used, 1) ||
      ng_io(c->fd, &reply, sizeof(reply), 0))
    ng_failure("batch transport");
  if (reply.magic != NG_MAGIC || reply.sequence != c->sequence ||
      reply.status || reply.bytes > capacity)
    ng_failure("batch rejected");
  if (ng_io(c->fd, output, reply.bytes, 0))
    ng_failure("query output");
  c->used = 0;
  return reply.bytes;
}
static size_t ng_flush(void *output, size_t capacity) {
  return ng_flush_context(ng_current(), output, capacity);
}
static void ng_enqueue(uint32_t opcode, const void *data, size_t size) {
  struct ios_kvm_native_gl_context *c = ng_current();
  if (size > NG_LIMIT - sizeof(struct ng_record))
    ng_failure("command limit");
  size_t padded = (size + 7) & ~(size_t)7;
  size_t bytes = sizeof(struct ng_record) + padded;
  if (c->used && (c->used >= (1u << 20) || bytes > (1u << 20) - c->used))
    ng_flush(NULL, 0);
  if (c->used > NG_LIMIT - bytes)
    ng_failure("batch limit");
  if (c->allocated < c->used + bytes) {
    size_t n = c->used + bytes;
    if (n < (1u << 20))
      n = 1u << 20;
    uint8_t *p = realloc(c->commands, n);
    if (!p)
      ng_failure("command allocation");
    c->commands = p;
    c->allocated = n;
  }
  struct ng_record header = {opcode, (uint32_t)size};
  memcpy(c->commands + c->used, &header, sizeof(header));
  if (size)
    memcpy(c->commands + c->used + sizeof(header), data, size);
  if (padded > size)
    memset(c->commands + c->used + sizeof(header) + size, 0, padded - size);
  c->used += bytes;
}
static size_t ng_request(uint32_t op, const void *data, size_t size,
                         void *output, size_t capacity) {
  ng_enqueue(op, data, size);
  return ng_flush(output, capacity);
}
static void ng_call(uint32_t op, const void *data, size_t size, void *output,
                    size_t capacity) {
  if (ng_request(op, data, size, output, capacity) != capacity)
    ng_failure("unexpected output length");
}
static int ng_shared_try_read(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                              void *);
#include "client-pixel-state.h"
#include "client-pointers.h"
#include "client-query.h"
#include "client-scalars.h"
#include "client-shared.h"
#include "client-sync.h"
/* Diagnostic implicit contexts are released; explicit contexts remain
 * owner-managed. */
void ios_kvm_native_gl_disconnect(void) {
  ios_kvm_native_gl_make_current(NULL);
}
