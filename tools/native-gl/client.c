/* Experimental native GLES proxy. Not installed into EAGL by the launcher. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include "io.h"
#include "protocol.h"
#include "schema.h"
#include <GLES3/gl3.h>
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
struct ng_client {
  int fd;
  uint32_t sequence;
  size_t used, allocated;
  uint8_t *commands;
  struct ng_string_entry *strings;
  unsigned string_count, string_capacity;
};
static pthread_key_t ng_key;
static pthread_once_t ng_once = PTHREAD_ONCE_INIT;
static void ng_failure(const char *reason) {
  fprintf(stderr, "ios-kvm native GL: %s\n", reason);
  abort();
}
static void ng_destroy(void *pointer) {
  struct ng_client *c = pointer;
  if (!c)
    return;
  close(c->fd);
  free(c->commands);
  for (unsigned i = 0; i < c->string_count; i++)
    free(c->strings[i].text);
  free(c->strings);
  free(c);
}
static void ng_key_init(void) {
  if (pthread_key_create(&ng_key, ng_destroy))
    ng_failure("TLS key");
}
static struct ng_client *ng_current(void) {
  pthread_once(&ng_once, ng_key_init);
  struct ng_client *c = pthread_getspecific(ng_key);
  if (c)
    return c;
  c = calloc(1, sizeof(*c));
  if (!c)
    ng_failure("client allocation");
  c->fd = -1;
  struct ng_hello hello = {
      .magic = NG_MAGIC, .version = NG_VERSION, .schema = NG_SCHEMA};
  if (ng_token(getenv("IOS_KVM_NATIVE_GL_TOKEN_FILE"), hello.token))
    ng_failure("private token file required");
  const char *host = getenv("IOS_KVM_NATIVE_GL_HOST"),
             *port = getenv("IOS_KVM_NATIVE_GL_PORT");
  char *end = NULL;
  unsigned long number = port ? strtoul(port, &end, 10) : 6733;
  if (number < 1024 || number > 65535 || (port && (!*port || *end)))
    ng_failure("invalid port");
  struct sockaddr_in address = {.sin_family = AF_INET,
                                .sin_port = htons(number)};
  if (inet_pton(AF_INET, host ? host : "10.0.2.2", &address.sin_addr) != 1)
    ng_failure("numeric IPv4 host required");
  c->fd = socket(AF_INET, SOCK_STREAM, 0);
  if (c->fd < 0)
    ng_failure("socket");
  ng_socket_options(c->fd);
  if (connect(c->fd, (struct sockaddr *)&address, sizeof(address)))
    ng_failure("connect");
  uint32_t ack = 0;
  if (ng_io(c->fd, &hello, sizeof(hello), 1) ||
      ng_io(c->fd, &ack, sizeof(ack), 0) || ack != NG_MAGIC)
    ng_failure("handshake");
  if (pthread_setspecific(ng_key, c))
    ng_failure("TLS value");
  return c;
}
static size_t ng_flush(void *output, size_t capacity) {
  struct ng_client *c = ng_current();
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
static void ng_enqueue(uint32_t opcode, const void *data, size_t size) {
  struct ng_client *c = ng_current();
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
#include "client-pointers.h"
#include "client-scalars.h"
/* Explicit diagnostic cleanup; the future EAGL adapter will own context
 * lifetime. */
void ios_kvm_native_gl_disconnect(void) {
  pthread_once(&ng_once, ng_key_init);
  struct ng_client *c = pthread_getspecific(ng_key);
  pthread_setspecific(ng_key, NULL);
  ng_destroy(c);
}
