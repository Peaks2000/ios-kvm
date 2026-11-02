/* Exercises the proxy with guest-created resources, shader text and padded
 * pixels. */
#define _POSIX_C_SOURCE 200809L
#include "context.h"
#include "protocol.h"
#include <GLES3/gl31.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void ios_kvm_native_gl_disconnect(void);
static void require(int ok, const char *reason) {
  if (!ok) {
    fprintf(stderr, "native GL check FAILED: %s\n", reason);
    exit(1);
  }
}
static GLuint shader(GLenum kind, const char *source) {
  GLuint s = glCreateShader(kind);
  require(s != 0, "shader name");
  const char *strings[] = {"#version 300 es\n", source};
  GLint lengths[] = {16, (GLint)strlen(source)};
  glShaderSource(s, 2, strings, lengths);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(s, sizeof(log), NULL, log);
    fprintf(stderr, "%s\n", log);
  }
  require(ok, "shader compilation");
  return s;
}
static void bad_shader(void) {
  GLuint s = glCreateShader(GL_FRAGMENT_SHADER);
  const char *bad = "#version 300 es\ninvalid shader syntax";
  glShaderSource(s, 1, &bad, NULL);
  glCompileShader(s);
  GLint ok = 1;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  require(!ok, "invalid shader status");
  unsigned char text[34];
  memset(text, 0xa5, sizeof(text));
  GLsizei length = -1;
  glGetShaderInfoLog(s, 16, &length, (char *)text + 1);
  require(length >= 0 && length < 16 && text[0] == 0xa5 && text[17] == 0xa5,
          "shader log bounds");
  require(text[1 + length] == 0, "shader log terminator");
  glDeleteShader(s);
}
static GLuint texture(unsigned w, unsigned h) {
  GLuint t;
  glGenTextures(1, &t);
  require(t != 0, "texture name");
  glBindTexture(GL_TEXTURE_2D, t);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               NULL);
  return t;
}
#include "query-check.h"
static void *isolation(void *argument) {
  unsigned color = (unsigned)(uintptr_t)argument;
  GLuint tex = texture(2, 2), fbo;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "thread FBO");
  for (unsigned i = 0; i < 10; i++) {
    glClearColor(color / 255.0f, i / 255.0f, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    uint8_t pixels[16];
    glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (unsigned p = 0; p < 4; p++)
      require(abs(pixels[p * 4] - (int)color) <= 1 &&
                  abs(pixels[p * 4 + 1] - (int)i) <= 1 &&
                  pixels[p * 4 + 3] == 255,
              "thread context isolation");
  }
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &tex);
  require(glGetError() == 0, "thread GL status");
  ios_kvm_native_gl_disconnect();
  return NULL;
}
static void large_surface(unsigned width, unsigned height, GLenum format) {
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  GLuint tex = texture(width, height), fbo;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "phone-size FBO");
  glViewport(0, 0, width, height);
  glClearColor(.25f, .5f, .75f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  size_t bytes = (size_t)width * height * 4;
  uint8_t *pixels = malloc(bytes);
  require(pixels != NULL, "phone-size allocation");
  memset(pixels, 0xa5, bytes);
  glReadPixels(0, 0, width, height, format, GL_UNSIGNED_BYTE, pixels);
  int expected[] = {format == NG_BGRA ? 191 : 64, 128,
                    format == NG_BGRA ? 64 : 191, 255};
  for (size_t i = 0; i < bytes; i++)
    require(abs((int)pixels[i] - expected[i % 4]) <= 1, "phone-size pixels");
  free(pixels);
  require(glGetError() == 0, "phone-size GL status");
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &tex);
  printf("Native proxy phone-size %ux%u format=%x: all pixels passed\n", width,
         height, format);
}
static void context_pixel(unsigned r, unsigned g, unsigned b) {
  uint8_t pixels[16];
  glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  for (unsigned i = 0; i < 4; i++)
    require(abs((int)pixels[i * 4] - (int)r) <= 1 &&
                abs((int)pixels[i * 4 + 1] - (int)g) <= 1 &&
                abs((int)pixels[i * 4 + 2] - (int)b) <= 1 &&
                pixels[i * 4 + 3] == 255,
            "explicit context pixels");
}
static void context_scene(unsigned r, unsigned g, unsigned b) {
  GLuint tex = texture(2, 2), fbo;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "explicit context FBO");
  glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
}
static void *context_busy(void *context) {
  require(!ios_kvm_native_gl_make_current(context),
          "context cannot be current on two threads");
  require(!ios_kvm_native_gl_context_destroy(context),
          "busy context cannot be destroyed");
  return NULL;
}
static void *context_handoff(void *context) {
  require(ios_kvm_native_gl_make_current(context),
          "context migrates after release");
  context_pixel(191, 0, 0);
  glClearColor(0, .5f, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  /* Thread exit must flush the pending clear and release, preserving the owned
   * context. */
  return NULL;
}
static void explicit_contexts(void) {
  struct ios_kvm_native_gl_context *a = ios_kvm_native_gl_context_create(3),
                                   *b = ios_kvm_native_gl_context_create(3);
  require(a && b, "explicit context creation");
  require(!ios_kvm_native_gl_context_create(2),
          "unsupported context version returns NULL");
  require(ios_kvm_native_gl_make_current(a), "bind A");
  context_scene(191, 0, 0);
  GLsync a_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  require(a_fence != NULL, "A fence");
  require(ios_kvm_native_gl_make_current(b), "bind B");
  context_scene(0, 0, 64);
  require(!glIsSync(a_fence), "fence belongs to its native context");
  GLsync b_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  require(b_fence && b_fence != a_fence, "distinct context fence tokens");
  glDeleteSync(b_fence);
  for (unsigned i = 0; i < 12; i++) {
    require(ios_kvm_native_gl_make_current(a), "switch to A");
    context_pixel(191, 0, 0);
    require(ios_kvm_native_gl_make_current(b), "switch to B");
    context_pixel(0, 0, 64);
  }
  require(ios_kvm_native_gl_make_current(a), "bind owned A");
  pthread_t worker;
  require(!pthread_create(&worker, NULL, context_busy, a), "busy worker");
  pthread_join(worker, NULL);
  context_pixel(191, 0, 0);
  require(ios_kvm_native_gl_make_current(NULL), "release A");
  require(!pthread_create(&worker, NULL, context_handoff, a), "handoff worker");
  pthread_join(worker, NULL);
  require(ios_kvm_native_gl_make_current(a), "rebind after worker exit");
  context_pixel(0, 128, 0);
  require(ios_kvm_native_gl_context_destroy(a), "destroy current A");
  require(ios_kvm_native_gl_make_current(b), "B survives A destruction");
  context_pixel(0, 0, 64);
  const char *port = getenv("IOS_KVM_NATIVE_GL_PORT");
  char *saved = port ? strdup(port) : NULL;
  setenv("IOS_KVM_NATIVE_GL_PORT", "65536", 1);
  require(!ios_kvm_native_gl_context_create(3),
          "failed context creation is recoverable");
  if (saved) {
    setenv("IOS_KVM_NATIVE_GL_PORT", saved, 1);
    free(saved);
  } else
    unsetenv("IOS_KVM_NATIVE_GL_PORT");
  context_pixel(0, 0, 64);
  require(ios_kvm_native_gl_make_current(NULL), "release B");
  require(ios_kvm_native_gl_context_destroy(b), "destroy released B");
  puts("Native proxy explicit contexts: switching, ownership, migration, "
       "thread exit and recovery passed");
}
int main(void) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  printf("Native proxy renderer: %s\n", glGetString(GL_RENDERER));
  const GLubyte *renderer = glGetString(GL_RENDERER);
  for (unsigned i = 0; i < 100; i++)
    require(glGetString(GL_RENDERER) == renderer, "stable cached GL string");
  GLint extensions = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
  for (GLint i = 0; i < extensions && i < 16; i++) {
    const GLubyte *extension = glGetStringi(GL_EXTENSIONS, i);
    require(extension && extension[0] &&
                glGetStringi(GL_EXTENSIONS, i) == extension,
            "indexed GL strings");
  }
  bad_shader();
  GLuint vs = shader(GL_VERTEX_SHADER,
                     "layout(location=0) in vec2 p;layout(location=1) in vec2 "
                     "uv;uniform mat4 transform;out vec2 v;void "
                     "main(){v=uv;gl_Position=transform*vec4(p,0,1);}");
  GLuint fs = shader(
      GL_FRAGMENT_SHADER,
      "precision highp float;uniform sampler2D image;uniform vec4 tint;in vec2 "
      "v;out vec4 pixel;void main(){pixel=texture(image,v)*tint;}");
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glBindAttribLocation(program, 0, "p");
  glBindAttribLocation(program, 1, "uv");
  glLinkProgram(program);
  GLint ok;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  require(ok, "program link");
  glUseProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint matrix = glGetUniformLocation(program, "transform"),
        tint = glGetUniformLocation(program, "tint"),
        sampler = glGetUniformLocation(program, "image");
  require(matrix >= 0 && tint >= 0 && sampler >= 0, "uniform locations");
  require(glGetAttribLocation(program, "uv") == 1, "attribute location");
  const GLfloat identity[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                white[] = {1, 1, 1, 1};
  glUniformMatrix4fv(matrix, 1, GL_FALSE, identity);
  glUniform4fv(tint, 1, white);
  glUniform1i(sampler, 0);
  query_program(program, vs, fs, matrix, tint, sampler);
  const GLfloat vertices[] = {-1, -1, 0, 0, 1, -1, 1, 0,
                              -1, 1,  0, 1, 1, 1,  1, 1};
  const GLushort indices[] = {0, 1, 2, 2, 1, 3};
  GLuint vao, buffers[2];
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(2, buffers);
  glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), NULL, GL_STATIC_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), NULL);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                        (void *)(2 * sizeof(GLfloat)));
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);
  enum { W = 65, H = 33, INPUT_ROW = 79, OUTPUT_ROW = 76, STRIDE = 304 };
  GLuint input = texture(W, H), output = texture(W, H), fbo;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         output, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "output framebuffer");
  glViewport(0, 0, W, H);
  GLint viewport[4] = {0};
  glGetIntegerv(GL_VIEWPORT, viewport);
  require(viewport[2] == W && viewport[3] == H, "vector query");
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, input);
  query_geometry(output);
  uint8_t source[(H + 2) * INPUT_ROW * 4], pixels[(H + 3) * STRIDE],
      expected[(H + 3) * STRIDE];
  for (unsigned frame = 0; frame < 20; frame++) {
    int bgra_upload = frame & 1, bgra_read = frame % 3 == 0;
    GLenum upload_format = bgra_upload ? NG_BGRA : GL_RGBA,
           read_format = bgra_read ? NG_BGRA : GL_RGBA;
    memset(source, 0x6b, sizeof(source));
    memset(pixels, 0xa5, sizeof(pixels));
    memset(expected, 0xa5, sizeof(expected));
    for (unsigned y = 0; y < H; y++)
      for (unsigned x = 0; x < W; x++) {
        uint8_t *p = source + ((y + 1) * INPUT_ROW + x + 3) * 4;
        p[0] = (x * 3 + frame * 7) & 255;
        p[1] = (y * 5 + frame * 9) & 255;
        p[2] = frame + 40;
        p[3] = 255;
        uint8_t *reference = expected + (y + 1) * STRIDE + (x + 2) * 4;
        memcpy(reference, p, 4);
        if (bgra_upload != bgra_read) {
          reference[0] = p[2];
          reference[2] = p[0];
        }
      }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, INPUT_ROW);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, upload_format,
                 GL_UNSIGNED_BYTE, source);
    uint8_t patch[10 * 64];
    memset(patch, 0x6b, sizeof(patch));
    for (unsigned y = 0; y < 9; y++)
      for (unsigned x = 0; x < 11; x++) {
        uint8_t *p = patch + (y + 1) * 64 + (x + 1) * 4;
        p[0] = frame + 90;
        p[1] = x * 13;
        p[2] = y * 17;
        p[3] = 255;
        uint8_t *reference = expected + (y + 6) * STRIDE + (x + 9) * 4;
        memcpy(reference, p, 4);
        if (bgra_upload != bgra_read) {
          reference[0] = p[2];
          reference[2] = p[0];
        }
      }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 15);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 7, 5, 11, 9, upload_format,
                    GL_UNSIGNED_BYTE, patch);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL);
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    require(fence && glIsSync(fence) && !glIsSync(NULL),
            "native fence identity");
    GLenum wait = glClientWaitSync(fence, 0, 0);
    require(wait == GL_TIMEOUT_EXPIRED || wait == GL_ALREADY_SIGNALED ||
                wait == GL_CONDITION_SATISFIED,
            "nonblocking native fence wait");
    glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
    wait = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
    require(wait == GL_ALREADY_SIGNALED || wait == GL_CONDITION_SATISFIED,
            "native GPU fence completion");
    GLint sync_values[2] = {-1, 0x12345678};
    GLsizei sync_length = -1;
    glGetSynciv(fence, GL_SYNC_STATUS, 1, &sync_length, sync_values);
    require(sync_length == 1 && sync_values[0] == GL_SIGNALED &&
                sync_values[1] == 0x12345678,
            "sync status and bounds");
    sync_length = -1;
    glGetSynciv(fence, GL_SYNC_STATUS, 0, &sync_length, sync_values);
    require(sync_length == 0 && sync_values[1] == 0x12345678,
            "zero-capacity sync query");
    glDeleteSync(fence);
    require(!glIsSync(fence), "deleted fence token is stale");
    glDeleteSync(NULL);

    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(GL_PACK_ROW_LENGTH, OUTPUT_ROW);
    glPixelStorei(GL_PACK_SKIP_ROWS, 1);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 2);
    glReadPixels(0, 0, W, H, read_format, GL_UNSIGNED_BYTE, pixels);
    unsigned errors = 0;
    for (size_t i = 0; i < sizeof(pixels); i++) {
      size_t y = i / STRIDE, x = i % STRIDE;
      int active = y >= 1 && y <= H && x >= 8 && x < 8 + W * 4;
      if (active ? abs((int)pixels[i] - expected[i]) > 1
                 : pixels[i] != expected[i])
        errors++;
    }
    require(!errors,
            "fresh texture/subimage/indexed draw/padded readback pixels");
    require(glGetError() == 0, "render GL status");
    printf("Native proxy frame %u: all active pixels and untouched padding "
           "passed\n",
           frame);
  }
  glEnable(0xdeadbeef);
  require(glGetError() == GL_INVALID_ENUM, "GL error preserved");
  require(glGetError() == GL_NO_ERROR, "GL error consumed once");
  compressed_pixels();
  large_surface(750, 1624, GL_RGBA);
  large_surface(1125, 2436, NG_BGRA);
  pthread_t a, b;
  require(!pthread_create(&a, NULL, isolation, (void *)(uintptr_t)51),
          "thread A");
  require(!pthread_create(&b, NULL, isolation, (void *)(uintptr_t)173),
          "thread B");
  pthread_join(a, NULL);
  pthread_join(b, NULL);
  glDeleteBuffers(2, buffers);
  glDeleteVertexArrays(1, &vao);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &input);
  glDeleteTextures(1, &output);
  glDeleteProgram(program);
  glFinish();
  require(glGetError() == 0, "cleanup");
  ios_kvm_native_gl_disconnect();
  explicit_contexts();
  puts("Native GL general API check PASSED (20 frames, padded input/output, "
       "shader errors, independent thread contexts)");
  return 0;
}
