/* Owned GPU snapshots for a future native presentation consumer. The caller
 * drains/preserves earlier GL errors before capture. Internal failures leave
 * the existing CPU readback path available. All leases count against the cap.
 * Consumers retain their leases until imported siblings and GPU work retire. */
#ifndef IOS_KVM_NATIVE_GPU_FRAMES_H
#define IOS_KVM_NATIVE_GPU_FRAMES_H
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NG_GPU_FRAME_LIMIT 8
#define NG_GPU_BYTE_LIMIT (UINT64_C(64) << 20)
struct ng_gpu_frame {
  struct ng_gpu_frame *next;
  EGLImageKHR image;
  EGLSyncKHR fence;
  uint64_t owner, cell, bytes;
  uint32_t sequence, references;
  GLsizei width, height;
  int published;
};
struct ng_gpu_frame_stats {
  uint64_t captured, evicted, dropped, failed, live_bytes;
  unsigned live_frames;
};
static struct {
  pthread_mutex_t lock;
  EGLDisplay display;
  PFNEGLCREATEIMAGEKHRPROC create_image;
  PFNEGLDESTROYIMAGEKHRPROC destroy_image;
  PFNEGLCREATESYNCKHRPROC create_fence;
  PFNEGLDESTROYSYNCKHRPROC destroy_fence;
  PFNEGLCLIENTWAITSYNCKHRPROC client_wait;
  PFNEGLWAITSYNCKHRPROC server_wait;
  struct ng_gpu_frame *frames;
  struct ng_gpu_frame_stats stats;
  uint64_t next_owner;
  int enabled;
} ng_gpu = {.lock = PTHREAD_MUTEX_INITIALIZER};
static _Thread_local uint64_t ng_gpu_owner;

static int ng_gpu_extension(const char *extensions, const char *name) {
  if (!extensions)
    return 0;
  size_t length = strlen(name);
  const char *found = extensions;
  while ((found = strstr(found, name))) {
    if ((found == extensions || found[-1] == ' ') &&
        (!found[length] || found[length] == ' '))
      return 1;
    found += length;
  }
  return 0;
}
/* Initialize once before accepting clients. A requested unsupported backend
 * fails initialization, giving the launcher a concrete capability result. */
static int ng_gpu_frames_init(EGLDisplay display, int enabled) {
  if (!enabled)
    return 0;
  const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
  if (!ng_gpu_extension(extensions, "EGL_KHR_image_base") ||
      !ng_gpu_extension(extensions, "EGL_KHR_gl_texture_2D_image") ||
      !ng_gpu_extension(extensions, "EGL_KHR_fence_sync") ||
      !ng_gpu_extension(extensions, "EGL_KHR_wait_sync"))
    return -1;
  ng_gpu.create_image = (void *)eglGetProcAddress("eglCreateImageKHR");
  ng_gpu.destroy_image = (void *)eglGetProcAddress("eglDestroyImageKHR");
  ng_gpu.create_fence = (void *)eglGetProcAddress("eglCreateSyncKHR");
  ng_gpu.destroy_fence = (void *)eglGetProcAddress("eglDestroySyncKHR");
  ng_gpu.client_wait = (void *)eglGetProcAddress("eglClientWaitSyncKHR");
  ng_gpu.server_wait = (void *)eglGetProcAddress("eglWaitSyncKHR");
  if (!ng_gpu.create_image || !ng_gpu.destroy_image || !ng_gpu.create_fence ||
      !ng_gpu.destroy_fence || !ng_gpu.client_wait || !ng_gpu.server_wait)
    return -1;
  ng_gpu.display = display;
  ng_gpu.enabled = 1;
  return 0;
}
static int ng_gpu_errors(void) {
  int failed = 0;
  for (unsigned i = 0; i < 32; i++) {
    if (glGetError() == GL_NO_ERROR)
      break;
    failed = 1;
  }
  return failed;
}
/* The list is oldest first. Called only while holding the store lock. */
static void ng_gpu_frame_destroy(struct ng_gpu_frame **link) {
  struct ng_gpu_frame *frame = *link;
  *link = frame->next;
  if (frame->fence != EGL_NO_SYNC_KHR)
    ng_gpu.destroy_fence(ng_gpu.display, frame->fence);
  if (frame->image != EGL_NO_IMAGE_KHR)
    ng_gpu.destroy_image(ng_gpu.display, frame->image);
  ng_gpu.stats.live_bytes -= frame->bytes;
  ng_gpu.stats.live_frames--;
  free(frame);
}
static inline struct ng_gpu_frame_stats ng_gpu_frame_stats_read(void) {
  pthread_mutex_lock(&ng_gpu.lock);
  struct ng_gpu_frame_stats stats = ng_gpu.stats;
  pthread_mutex_unlock(&ng_gpu.lock);
  return stats;
}
/* Call after the request's RAM spans, completion cell and dimensions validate.
 * The key is connection-owned. A future display bridge must supply an exact
 * owner and generation, rather than selecting a frame solely by address. */
static int ng_gpu_frame_capture(GLint x, GLint y, GLsizei width, GLsizei height,
                                 uint64_t cell, uint32_t sequence) {
  if (!ng_gpu.enabled)
    return 0;
  if (x < 0 || y < 0 || width <= 0 || height <= 0 || width > 4096 ||
      height > 4096 || !cell || !sequence)
    return 0;
  uint64_t bytes = (uint64_t)width * height * 4;
  pthread_mutex_lock(&ng_gpu.lock);
  if (!ng_gpu_owner) {
    if (ng_gpu.next_owner == UINT64_MAX) {
      pthread_mutex_unlock(&ng_gpu.lock);
      return 0;
    }
    ng_gpu_owner = ++ng_gpu.next_owner;
  }
  for (struct ng_gpu_frame *frame = ng_gpu.frames; frame; frame = frame->next)
    if (frame->owner == ng_gpu_owner && frame->cell == cell &&
        frame->sequence == sequence) {
      ng_gpu.stats.dropped++;
      pthread_mutex_unlock(&ng_gpu.lock);
      return 0;
    }
  while (ng_gpu.stats.live_frames == NG_GPU_FRAME_LIMIT ||
         ng_gpu.stats.live_bytes > NG_GPU_BYTE_LIMIT - bytes) {
    struct ng_gpu_frame **link = &ng_gpu.frames;
    while (*link && (!(*link)->published || (*link)->references != 1))
      link = &(*link)->next;
    if (!*link) {
      ng_gpu.stats.dropped++;
      pthread_mutex_unlock(&ng_gpu.lock);
      return 0;
    }
    ng_gpu_frame_destroy(link);
    ng_gpu.stats.evicted++;
  }
  struct ng_gpu_frame *frame = calloc(1, sizeof(*frame));
  if (!frame) {
    ng_gpu.stats.failed++;
    pthread_mutex_unlock(&ng_gpu.lock);
    return 0;
  }
  GLint previous = 0;
  GLuint texture = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
  glGenTextures(1, &texture);
  if (texture) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x, y, width, height, 0);
  }
  int failed = ng_gpu_errors() || !texture;
  if (!failed) {
    const EGLint attributes[] = {EGL_GL_TEXTURE_LEVEL_KHR, 0,
                                 EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    frame->image = ng_gpu.create_image(ng_gpu.display, eglGetCurrentContext(),
                                      EGL_GL_TEXTURE_2D_KHR,
                                      (EGLClientBuffer)(uintptr_t)texture,
                                      attributes);
    failed = frame->image == EGL_NO_IMAGE_KHR;
  }
  if (!failed) {
    frame->fence = ng_gpu.create_fence(ng_gpu.display, EGL_SYNC_FENCE_KHR, NULL);
    failed = frame->fence == EGL_NO_SYNC_KHR;
  }
  glBindTexture(GL_TEXTURE_2D, previous);
  glDeleteTextures(1, &texture);
  glFlush();
  failed |= ng_gpu_errors();
  if (failed) {
    if (frame->fence != EGL_NO_SYNC_KHR)
      ng_gpu.destroy_fence(ng_gpu.display, frame->fence);
    if (frame->image != EGL_NO_IMAGE_KHR)
      ng_gpu.destroy_image(ng_gpu.display, frame->image);
    free(frame);
    ng_gpu.stats.failed++;
    pthread_mutex_unlock(&ng_gpu.lock);
    return 0;
  }
  *frame = (struct ng_gpu_frame){.image = frame->image, .fence = frame->fence,
                                 .owner = ng_gpu_owner, .cell = cell,
                                 .bytes = bytes, .sequence = sequence,
                                 .references = 1, .width = width,
                                 .height = height, .published = 1};
  struct ng_gpu_frame **tail = &ng_gpu.frames;
  while (*tail)
    tail = &(*tail)->next;
  *tail = frame;
  ng_gpu.stats.live_bytes += bytes;
  ng_gpu.stats.live_frames++;
  ng_gpu.stats.captured++;
  pthread_mutex_unlock(&ng_gpu.lock);
  return 1;
}
static inline struct ng_gpu_frame *ng_gpu_frame_acquire(uint64_t owner, uint64_t cell,
                                                 uint32_t sequence) {
  pthread_mutex_lock(&ng_gpu.lock);
  struct ng_gpu_frame *frame = ng_gpu.frames;
  while (frame && (!frame->published || frame->owner != owner ||
                   frame->cell != cell || frame->sequence != sequence))
    frame = frame->next;
  if (frame && frame->references != UINT32_MAX)
    frame->references++;
  else
    frame = NULL;
  pthread_mutex_unlock(&ng_gpu.lock);
  return frame;
}
static inline void ng_gpu_frame_release(struct ng_gpu_frame *frame) {
  if (!frame)
    return;
  pthread_mutex_lock(&ng_gpu.lock);
  if (--frame->references == 0) {
    struct ng_gpu_frame **link = &ng_gpu.frames;
    while (*link != frame)
      link = &(*link)->next;
    ng_gpu_frame_destroy(link);
  }
  pthread_mutex_unlock(&ng_gpu.lock);
}
/* Owner shutdown revokes future acquisition. Existing leases keep images and
 * fences alive until their consumers release them. Context sharing is unused. */
static void ng_gpu_frames_owner_close(void) {
  if (!ng_gpu_owner)
    return;
  pthread_mutex_lock(&ng_gpu.lock);
  struct ng_gpu_frame **link = &ng_gpu.frames;
  while (*link) {
    struct ng_gpu_frame *frame = *link;
    if (frame->owner == ng_gpu_owner && frame->published) {
      frame->published = 0;
      if (--frame->references == 0) {
        ng_gpu_frame_destroy(link);
        continue;
      }
    }
    link = &frame->next;
  }
  ng_gpu_owner = 0;
  pthread_mutex_unlock(&ng_gpu.lock);
}
#endif
