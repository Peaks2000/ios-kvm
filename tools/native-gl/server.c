/* Execute a bounded GLES command stream natively, in an isolated EGL context.
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE 1
#include "io.h"
#include "protocol.h"
#include "schema.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static EGLDisplay ng_display;
static EGLConfig ng_config;
static uint8_t ng_secret[NG_TOKEN_BYTES];
static _Atomic unsigned ng_clients;
static int ng_output(void *output, size_t capacity, size_t *used,
                     const void *data, size_t size) {
  if (*used || size > capacity)
    return -1;
  if (size)
    memcpy(output, data, size);
  *used = size;
  return 0;
}
#include "server-pointers.h"
static int ng_dispatch(uint32_t opcode, const uint8_t *data, size_t size,
                       void *output, size_t capacity, size_t *used) {
  switch (opcode) {
#include "server-scalars.h"
  default:
    return ng_pointer(opcode, data, size, output, capacity, used);
  }
}
static void *ng_serve(void *argument) {
  int fd = (int)(intptr_t)argument;
  ng_socket_options(fd);
  struct ng_hello hello;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface surface = EGL_NO_SURFACE;
  uint8_t *commands = NULL, *output = NULL;
  if (ng_io(fd, &hello, sizeof(hello), 0) || hello.magic != NG_MAGIC ||
      hello.version != NG_VERSION || hello.schema != NG_SCHEMA ||
      memcmp(hello.token, ng_secret, NG_TOKEN_BYTES))
    goto done;
  const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE},
               sa[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  if (!eglBindAPI(EGL_OPENGL_ES_API))
    goto done;
  context = eglCreateContext(ng_display, ng_config, EGL_NO_CONTEXT, ca);
  surface = eglCreatePbufferSurface(ng_display, ng_config, sa);
  if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
      !eglMakeCurrent(ng_display, surface, surface, context))
    goto done;
  uint32_t ack = NG_MAGIC;
  if (ng_io(fd, &ack, sizeof(ack), 1))
    goto done;
  fprintf(stdout, "native GLES client: %s\n", glGetString(GL_RENDERER));
  fflush(stdout);
  uint32_t sequence = 0;
  for (;;) {
    struct ng_batch batch;
    if (ng_io(fd, &batch, sizeof(batch), 0))
      break;
    if (batch.magic != NG_MAGIC || sequence == UINT32_MAX ||
        batch.sequence != ++sequence || !batch.bytes ||
        batch.bytes > NG_LIMIT || batch.reply_capacity > NG_LIMIT)
      break;
    commands = malloc(batch.bytes);
    output = calloc(1, batch.reply_capacity ? batch.reply_capacity : 1);
    if (!commands || !output)
      break;
    if (ng_io(fd, commands, batch.bytes, 0))
      break;
    size_t at = 0, used = 0;
    int status = 0;
    while (at < batch.bytes) {
      if (batch.bytes - at < sizeof(struct ng_record)) {
        status = -1;
        break;
      }
      struct ng_record record;
      memcpy(&record, commands + at, sizeof(record));
      at += sizeof(record);
      size_t padded = ((size_t)record.bytes + 7) & ~(size_t)7;
      if (record.bytes > batch.bytes - at || padded > batch.bytes - at ||
          used) {
        status = -1;
        break;
      }
      if (ng_dispatch(record.opcode, commands + at, record.bytes, output,
                      batch.reply_capacity, &used)) {
        status = -1;
        break;
      }
      at += padded;
    }
    struct ng_reply reply = {NG_MAGIC, sequence, (uint32_t)status,
                             (uint32_t)used};
    if (ng_io(fd, &reply, sizeof(reply), 1) ||
        (!status && ng_io(fd, output, used, 1)))
      break;
    free(commands);
    free(output);
    commands = output = NULL;
    if (status)
      break;
  }
done:
  free(commands);
  free(output);
  eglMakeCurrent(ng_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (surface != EGL_NO_SURFACE)
    eglDestroySurface(ng_display, surface);
  if (context != EGL_NO_CONTEXT)
    eglDestroyContext(ng_display, context);
  close(fd);
  atomic_fetch_sub(&ng_clients, 1);
  return NULL;
}
/* Match the already selected render node, including on multi-GPU Mesa hosts. */
static EGLDisplay ng_choose_display(void) {
  const char *node = getenv("VTEST_RENDERNODE"),
             *software = getenv("LIBGL_ALWAYS_SOFTWARE");
  if (!node || (software && !strcmp(software, "1")))
    return eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                 EGL_DEFAULT_DISPLAY, NULL);
  PFNEGLQUERYDEVICESEXTPROC query =
      (void *)eglGetProcAddress("eglQueryDevicesEXT");
  PFNEGLQUERYDEVICESTRINGEXTPROC device_string =
      (void *)eglGetProcAddress("eglQueryDeviceStringEXT");
  PFNEGLGETPLATFORMDISPLAYEXTPROC display_for =
      (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
  EGLDeviceEXT devices[32];
  EGLint count = 0;
  if (!query || !device_string || !display_for || !query(32, devices, &count))
    return EGL_NO_DISPLAY;
  if (count > 32)
    count = 32;
  struct stat requested;
  if (stat(node, &requested) || !S_ISCHR(requested.st_mode))
    return EGL_NO_DISPLAY;
  for (EGLint i = 0; i < count; i++) {
    const char *candidate =
        device_string(devices[i], EGL_DRM_RENDER_NODE_FILE_EXT);
    struct stat actual;
    if (candidate && !stat(candidate, &actual) && S_ISCHR(actual.st_mode) &&
        actual.st_rdev == requested.st_rdev) {
      fprintf(stdout, "native GLES render node: %s\n", candidate);
      return display_for(EGL_PLATFORM_DEVICE_EXT, devices[i], NULL);
    }
  }
  fprintf(stderr, "native GLES: selected render node is unavailable\n");
  return EGL_NO_DISPLAY;
}
int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr,
            "usage: native-gl-server PRIVATE_TOKEN_FILE LOOPBACK_PORT\n");
    return 1;
  }
  if (ng_token(argv[1], ng_secret)) {
    fprintf(stderr, "Expected an owned, private,32-byte token file\n");
    return 1;
  }
  char *end;
  unsigned long port = strtoul(argv[2], &end, 10);
  if (!*argv[2] || *end || port < 1024 || port > 65535)
    return 1;
  ng_display = ng_choose_display();
  if (ng_display == EGL_NO_DISPLAY || !eglInitialize(ng_display, NULL, NULL) ||
      !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  const EGLint attrs[] = {EGL_SURFACE_TYPE,
                          EGL_PBUFFER_BIT,
                          EGL_RENDERABLE_TYPE,
                          EGL_OPENGL_ES3_BIT,
                          EGL_RED_SIZE,
                          8,
                          EGL_GREEN_SIZE,
                          8,
                          EGL_BLUE_SIZE,
                          8,
                          EGL_ALPHA_SIZE,
                          8,
                          EGL_DEPTH_SIZE,
                          24,
                          EGL_STENCIL_SIZE,
                          8,
                          EGL_NONE};
  EGLint count;
  if (!eglChooseConfig(ng_display, attrs, &ng_config, 1, &count) || count != 1)
    return 1;
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0)
    return 1;
  int on = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  struct sockaddr_in address = {.sin_family = AF_INET,
                                .sin_port = htons(port),
                                .sin_addr = {htonl(INADDR_LOOPBACK)}};
  if (bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
      listen(listener, 16)) {
    perror("native GL listen");
    return 1;
  }
  printf("native GLES research server on loopback:%lu\n", port);
  fflush(stdout);
  for (;;) {
    int fd = accept(listener, NULL, NULL);
    if (fd < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (atomic_fetch_add(&ng_clients, 1) >= 16) {
      atomic_fetch_sub(&ng_clients, 1);
      close(fd);
      continue;
    }
    pthread_t thread;
    if (pthread_create(&thread, NULL, ng_serve, (void *)(intptr_t)fd)) {
      atomic_fetch_sub(&ng_clients, 1);
      close(fd);
    } else
      pthread_detach(thread);
  }
  close(listener);
  return 0;
}
