/* GPU snapshot lifetime and pixel regression checks. */
#include "gpu-frame-fixture.h"
static void frozen_frame(struct test_context consumer, unsigned width,
                          unsigned height) {
  struct test_context producer = context_create();
  context_current(producer);
  target_create(width, height);
  pattern_draw(width, height, 3);
  glActiveTexture(GL_TEXTURE3);
  GLuint bound;
  glGenTextures(1, &bound);
  glBindTexture(GL_TEXTURE_2D, bound);
  glViewport(7, 9, 31, 37);
  glScissor(11, 13, 19, 23);
  glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
  glEnable(0xdeadbeef);
  ng_errors_collect();
  require(ng_gpu_frame_capture(0, 0, width, height, 0x1000, 7, 42), "GPU capture");
  require(ng_error_get() == GL_INVALID_ENUM && ng_error_get() == GL_NO_ERROR,
          "earlier client error retained exactly once");
  GLint binding, active, viewport[4], scissor[4], mask[4];
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetIntegerv(GL_SCISSOR_BOX, scissor);
  glGetIntegerv(GL_COLOR_WRITEMASK, mask);
  require(binding == (GLint)bound && active == GL_TEXTURE3 && viewport[0] == 7 &&
          viewport[1] == 9 && viewport[2] == 31 && viewport[3] == 37 &&
          scissor[0] == 11 && scissor[1] == 13 && scissor[2] == 19 &&
          scissor[3] == 23 && !mask[0] && mask[1] && !mask[2] && mask[3] &&
          glIsEnabled(GL_SCISSOR_TEST), "producer state restored");
  uint64_t owner = ng_gpu_owner;
  struct ng_gpu_frame *frame = ng_gpu_frame_acquire(owner, 0x1000, 7, 42);
  require(!ng_gpu_frame_acquire(owner + 1, 0x1000, 7, 42) &&
          !ng_gpu_frame_acquire(owner, 0x1001, 7, 42) &&
          !ng_gpu_frame_acquire(owner, 0x1000, 8, 42), "exact owner and generation");
  require(!ng_gpu_frame_capture(0, 0, width, height, 0x1000, 7, 42),
          "duplicate key refused");
  pattern_draw(width, height, 9);
  ng_gpu_frames_owner_close();
  require(!ng_gpu_frame_acquire(owner, 0x1000, 7, 42), "closed owner revoked");
  context_current(consumer);
  context_destroy(producer);
  frame_verify(frame, 3);
  ng_gpu_frame_release(frame);
  require(!ng_gpu_frame_stats_read().live_frames, "orphaned image lease freed");
  printf("Native GPU frozen frame PASSED (%ux%u, sampled after source rewrite "
         "and context destruction)\n", width, height);
}
static void frame_limits(struct test_context consumer) {
  struct test_context producer = context_create();
  context_current(producer);
  target_create(257, 131);
  struct ng_gpu_frame *held[NG_GPU_FRAME_LIMIT];
  for (unsigned i = 0; i < NG_GPU_FRAME_LIMIT; i++) {
    pattern_draw(257, 131, i);
    require(ng_gpu_frame_capture(0, 0, 257, 131, 0x2000, i + 1, 42),
            "capture into bounded ring");
    held[i] = ng_gpu_frame_acquire(ng_gpu_owner, 0x2000, i + 1, 42);
    require(held[i] != NULL, "hold each frame");
  }
  require(!ng_gpu_frame_capture(0, 0, 257, 131, 0x2000, 99, 42),
          "all leased slots cause bounded refusal");
  ng_gpu_frame_release(held[0]);
  held[0] = NULL;
  require(ng_gpu_frame_capture(0, 0, 257, 131, 0x2000, 99, 42),
          "released slot can be evicted");
  require(!ng_gpu_frame_acquire(ng_gpu_owner, 0x2000, 1, 42), "old generation evicted");
  ng_gpu_frames_owner_close();
  context_current(consumer);
  context_destroy(producer);
  for (unsigned i = 1; i < NG_GPU_FRAME_LIMIT; i++) {
    frame_verify(held[i], i);
    ng_gpu_frame_release(held[i]);
  }
  require(!ng_gpu_frame_stats_read().live_frames, "leased ring cleanup");
  producer = context_create();
  context_current(producer);
  target_create(1125, 2436);
  pattern_draw(1125, 2436, 2);
  for (unsigned i = 1; i <= 8; i++)
    require(ng_gpu_frame_capture(0, 0, 1125, 2436, 0x3000, i, 42),
            "phone-size GPU budget eviction");
  struct ng_gpu_frame_stats stats = ng_gpu_frame_stats_read();
  require(stats.live_bytes <= NG_GPU_BYTE_LIMIT && stats.live_frames == 6 &&
          stats.evicted >= 3, "byte budget enforced before slot limit");
  ng_gpu_frames_owner_close();
  context_current(consumer);
  context_destroy(producer);
  puts("Native GPU frame limits PASSED (leased slots, eviction and 64 MiB cap)");
}
static void failed_capture(void) {
  GLuint incomplete;
  glGenFramebuffers(1, &incomplete);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, incomplete);
  GLint before, after;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &before);
  require(!ng_gpu_frame_capture(0, 0, 16, 16, 0x5000, 1, 42),
          "incomplete source capture declined");
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &after);
  require(before == after && glGetError() == GL_NO_ERROR,
          "failed capture restores binding and consumes its internal error");
  ng_gpu_frames_owner_close();
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &incomplete);
  require(!ng_gpu_frame_stats_read().live_frames, "failed capture releases resources");
}
static void generation_keys(struct test_context consumer) {
  struct test_context producer = context_create();
  context_current(producer);
  target_create(257, 131);
  pattern_draw(257, 131, 3);
  require(ng_gpu_frame_capture(0, 0, 257, 131, 0x6000, 1, 100), "first surface lifetime");
  struct ios_kvm_surface_frame_key old_key = {.epoch = 100, .cell = 0x6000, .sequence = 1};
  struct ng_gpu_frame *old = ng_gpu_frame_acquire_key(&old_key);
  require(old != NULL, "acquire first lifetime");
  pattern_draw(257, 131, 9);
  require(ng_gpu_frame_capture(0, 0, 257, 131, 0x6000, 1, 101), "reused address and sequence");
  struct ios_kvm_surface_frame_key new_key = {.epoch = 101, .cell = 0x6000, .sequence = 1};
  struct ng_gpu_frame *current = ng_gpu_frame_acquire_key(&new_key);
  require(current && current != old, "new lifetime selects its own frozen image");
  struct ios_kvm_surface_frame_key invalid = new_key;
  invalid.epoch = 0;
  require(!ng_gpu_frame_acquire_key(&invalid), "legacy key cannot select a global frame");
  invalid = new_key;
  invalid.reserved = 1;
  require(!ng_gpu_frame_acquire_key(&invalid), "unknown key fields rejected");
  uint64_t first_owner = ng_gpu_owner;
  ng_gpu_owner = 0; /* Simulate a second connection replaying the same key. */
  require(ng_gpu_frame_capture(0, 0, 257, 131, 0x6000, 1, 101), "second connection key");
  uint64_t second_owner = ng_gpu_owner;
  require(!ng_gpu_frame_acquire_key(&new_key), "ambiguous key refused");
  ng_gpu_owner = first_owner;
  ng_gpu_frames_owner_close();
  struct ng_gpu_frame *remaining = ng_gpu_frame_acquire_key(&new_key);
  require(remaining != NULL, "remaining unique owner can be selected");
  ng_gpu_frame_release(remaining);
  ng_gpu_owner = second_owner;
  ng_gpu_frames_owner_close();
  context_current(consumer);
  context_destroy(producer);
  frame_verify(old, 3);
  frame_verify(current, 9);
  ng_gpu_frame_release(old);
  ng_gpu_frame_release(current);
  require(!ng_gpu_frame_stats_read().live_frames, "generation-key leases released");
  puts("Native GPU generation keys PASSED (address reuse, exact lifetime, ambiguity and revocation)");
}
struct thread_result {
  struct ng_gpu_frame *frame;
  unsigned number;
};
static void *thread_producer(void *pointer) {
  struct thread_result *result = pointer;
  require(eglBindAPI(EGL_OPENGL_ES_API), "worker GLES API");
  struct test_context producer = context_create();
  context_current(producer);
  target_create(257, 131);
  pattern_draw(257, 131, result->number);
  require(ng_gpu_frame_capture(0, 0, 257, 131, 0x4000, 1, 42), "worker capture");
  result->frame = ng_gpu_frame_acquire(ng_gpu_owner, 0x4000, 1, 42);
  ng_gpu_frames_owner_close();
  require(eglMakeCurrent(ng_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         EGL_NO_CONTEXT), "worker releases context");
  context_destroy(producer);
  return NULL;
}
int main(void) {
  ng_display = ng_choose_display();
  require(ng_display != EGL_NO_DISPLAY && eglInitialize(ng_display, NULL, NULL) &&
          eglBindAPI(EGL_OPENGL_ES_API), "initialize selected EGL renderer");
  const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  EGLint count;
  require(eglChooseConfig(ng_display, attributes, &ng_config, 1, &count) && count,
          "choose EGL configuration");
  struct test_context consumer = context_create();
  context_current(consumer);
  printf("Native GPU frame renderer: %s\n", glGetString(GL_RENDERER));
  require(!ng_gpu_frames_init(ng_display, 0) &&
          !ng_gpu_frame_capture(0, 0, 16, 16, 0x1000, 1, 42), "disabled mode");
  require(!ng_gpu_frames_init(ng_display, 1), "GPU image and fence capabilities");
  frozen_frame(consumer, 257, 131);
  frozen_frame(consumer, 750, 1624);
  frozen_frame(consumer, 1125, 2436);
  frame_limits(consumer);
  generation_keys(consumer);
  failed_capture();
  struct thread_result results[2] = {{.number = 4}, {.number = 11}};
  pthread_t threads[2];
  for (unsigned i = 0; i < 2; i++)
    require(!pthread_create(&threads[i], NULL, thread_producer, results + i),
            "start concurrent GPU producer");
  for (unsigned i = 0; i < 2; i++) {
    require(!pthread_join(threads[i], NULL), "join GPU producer");
    frame_verify(results[i].frame, results[i].number);
    ng_gpu_frame_release(results[i].frame);
  }
  struct ng_gpu_frame_stats stats = ng_gpu_frame_stats_read();
  require(!stats.live_frames && !stats.live_bytes && stats.failed == 1,
          "all GPU resources released with only the intentional failure");
  require(eglMakeCurrent(ng_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         EGL_NO_CONTEXT), "release consumer");
  context_destroy(consumer);
  eglTerminate(ng_display);
  printf("Native GPU frame store PASSED (captures=%llu evictions=%llu "
         "drops=%llu, cross-context GPU sampling, worker exit, full pixels, "
         "padding and bounded lifetimes)\n", (unsigned long long)stats.captured,
         (unsigned long long)stats.evicted, (unsigned long long)stats.dropped);
  return 0;
}
