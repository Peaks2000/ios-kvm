/* Exercise the actual server's GPU-only snapshot path across independent EGL
 * contexts. CPU reads occur only in the consumer's pixel verification. */
#define main ng_server_main
#include "server.c"
#undef main
#include <GLES2/gl2ext.h>

static void require(int ok, const char *reason) {
  if (!ok) {
    fprintf(stderr, "Native GPU frame check FAILED: %s\n", reason);
    exit(1);
  }
}
struct test_context {
  EGLContext context;
  EGLSurface surface;
};
static struct test_context context_create(void) {
  const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  const EGLint surface_attributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  struct test_context result = {
      eglCreateContext(ng_display, ng_config, EGL_NO_CONTEXT, context_attributes),
      eglCreatePbufferSurface(ng_display, ng_config, surface_attributes)};
  require(result.context != EGL_NO_CONTEXT && result.surface != EGL_NO_SURFACE,
          "independent GLES3 context");
  return result;
}
static void context_current(struct test_context context) {
  require(eglMakeCurrent(ng_display, context.surface, context.surface,
                         context.context), "make test context current");
}
static void context_destroy(struct test_context context) {
  require(eglDestroySurface(ng_display, context.surface), "destroy surface");
  require(eglDestroyContext(ng_display, context.context), "destroy context");
}
static GLuint target_create(unsigned width, unsigned height) {
  GLuint texture, framebuffer;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "source/output framebuffer complete");
  return texture;
}
static void pattern_color(unsigned frame, unsigned quadrant, uint8_t rgba[4]) {
  rgba[0] = 17 + frame * 7;
  rgba[1] = 31 + quadrant * 43;
  rgba[2] = 211 - frame * 3 - quadrant * 5;
  rgba[3] = 191 + quadrant * 11;
}
static void pattern_draw(unsigned width, unsigned height, unsigned frame) {
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glEnable(GL_SCISSOR_TEST);
  for (unsigned quadrant = 0; quadrant < 4; quadrant++) {
    unsigned x = quadrant % 2 ? width / 2 : 0;
    unsigned y = quadrant / 2 ? height / 2 : 0;
    unsigned w = quadrant % 2 ? width - x : width / 2;
    unsigned h = quadrant / 2 ? height - y : height / 2;
    uint8_t rgba[4];
    pattern_color(frame, quadrant, rgba);
    glScissor(x, y, w, h);
    glClearColor(rgba[0] / 255.f, rgba[1] / 255.f, rgba[2] / 255.f,
                  rgba[3] / 255.f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  require(glGetError() == GL_NO_ERROR, "draw changing source pixels");
}
static GLuint shader(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  require(success, "consumer shader compile");
  return shader;
}
static void frame_verify(struct ng_gpu_frame *frame, unsigned number) {
  require(frame != NULL, "owned frame lease");
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC import_image =
      (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
  require(import_image != NULL, "EGLImage texture import function");
  require(ng_gpu.server_wait(ng_display, frame->fence, 0),
          "GPU-side wait in unrelated consumer context");
  GLuint texture, output, framebuffer, vertex_array;
  glActiveTexture(GL_TEXTURE0);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  import_image(GL_TEXTURE_2D, frame->image);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  GLuint vertex = shader(GL_VERTEX_SHADER,
      "#version 300 es\nvoid main(){const vec2 p[4]=vec2[4](vec2(-1,-1),"
      "vec2(1,-1),vec2(-1,1),vec2(1,1));gl_Position=vec4(p[gl_VertexID],0,1);}");
  GLuint fragment = shader(GL_FRAGMENT_SHADER,
      "#version 300 es\nprecision highp float;uniform sampler2D image;"
      "out vec4 color;void main(){color=texelFetch(image,ivec2(gl_FragCoord.xy),0);}");
  GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  GLint success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  require(success, "consumer shader link");
  output = target_create(frame->width, frame->height);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *)&framebuffer);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "image"), 0);
  glGenVertexArrays(1, &vertex_array);
  glBindVertexArray(vertex_array);
  glViewport(0, 0, frame->width, frame->height);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  unsigned row = (frame->width + 3) * 4;
  size_t bytes = (size_t)row * (frame->height + 1);
  uint8_t *pixels = malloc(bytes);
  require(pixels != NULL, "consumer pixel verification allocation");
  memset(pixels, 0xa5, bytes);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, frame->width + 3);
  glReadPixels(0, 0, frame->width, frame->height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  for (unsigned y = 0; y <= (unsigned)frame->height; y++)
    for (unsigned x = 0; x < row; x++) {
      uint8_t expected = 0xa5;
      if (y < (unsigned)frame->height && x < (unsigned)frame->width * 4) {
        unsigned quadrant = (y >= (unsigned)frame->height / 2) * 2 +
                             (x / 4 >= (unsigned)frame->width / 2);
        uint8_t rgba[4];
        pattern_color(number, quadrant, rgba);
        expected = rgba[x % 4];
      }
      require(pixels[(size_t)y * row + x] == expected,
              "GPU image sampling matches every expected pixel and padding");
    }
  free(pixels);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glDeleteProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  glDeleteVertexArrays(1, &vertex_array);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(1, &output);
  glDeleteTextures(1, &texture);
  require(glGetError() == GL_NO_ERROR, "consumer GL errors");
}
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
  require(ng_gpu_frame_capture(0, 0, width, height, 0x1000, 7), "GPU capture");
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
  struct ng_gpu_frame *frame = ng_gpu_frame_acquire(owner, 0x1000, 7);
  require(!ng_gpu_frame_acquire(owner + 1, 0x1000, 7) &&
          !ng_gpu_frame_acquire(owner, 0x1001, 7) &&
          !ng_gpu_frame_acquire(owner, 0x1000, 8), "exact owner and generation");
  require(!ng_gpu_frame_capture(0, 0, width, height, 0x1000, 7),
          "duplicate key refused");
  pattern_draw(width, height, 9);
  ng_gpu_frames_owner_close();
  require(!ng_gpu_frame_acquire(owner, 0x1000, 7), "closed owner revoked");
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
    require(ng_gpu_frame_capture(0, 0, 257, 131, 0x2000, i + 1),
            "capture into bounded ring");
    held[i] = ng_gpu_frame_acquire(ng_gpu_owner, 0x2000, i + 1);
    require(held[i] != NULL, "hold each frame");
  }
  require(!ng_gpu_frame_capture(0, 0, 257, 131, 0x2000, 99),
          "all leased slots cause bounded refusal");
  ng_gpu_frame_release(held[0]);
  held[0] = NULL;
  require(ng_gpu_frame_capture(0, 0, 257, 131, 0x2000, 99),
          "released slot can be evicted");
  require(!ng_gpu_frame_acquire(ng_gpu_owner, 0x2000, 1), "old generation evicted");
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
    require(ng_gpu_frame_capture(0, 0, 1125, 2436, 0x3000, i),
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
  require(!ng_gpu_frame_capture(0, 0, 16, 16, 0x5000, 1),
          "incomplete source capture declined");
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &after);
  require(before == after && glGetError() == GL_NO_ERROR,
          "failed capture restores binding and consumes its internal error");
  ng_gpu_frames_owner_close();
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &incomplete);
  require(!ng_gpu_frame_stats_read().live_frames, "failed capture releases resources");
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
  require(ng_gpu_frame_capture(0, 0, 257, 131, 0x4000, 1), "worker capture");
  result->frame = ng_gpu_frame_acquire(ng_gpu_owner, 0x4000, 1);
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
          !ng_gpu_frame_capture(0, 0, 16, 16, 0x1000, 1), "disabled mode");
  require(!ng_gpu_frames_init(ng_display, 1), "GPU image and fence capabilities");
  frozen_frame(consumer, 257, 131);
  frozen_frame(consumer, 750, 1624);
  frozen_frame(consumer, 1125, 2436);
  frame_limits(consumer);
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
