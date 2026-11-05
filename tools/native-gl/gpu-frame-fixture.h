/* Shared renderer setup, spatial patterns and full-pixel consumer checks. */
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
static void frame_verify_image(EGLImageKHR image, unsigned width,
                                 unsigned height, unsigned number) {
  require(image != EGL_NO_IMAGE_KHR, "GPU image handle");
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC import_image =
      (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
  require(import_image != NULL, "EGLImage texture import function");
  GLuint texture, output, framebuffer, vertex_array;
  glActiveTexture(GL_TEXTURE0);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  import_image(GL_TEXTURE_2D, image);
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
  output = target_create(width, height);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *)&framebuffer);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "image"), 0);
  glGenVertexArrays(1, &vertex_array);
  glBindVertexArray(vertex_array);
  glViewport(0, 0, width, height);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  unsigned row = (width + 3) * 4;
  size_t bytes = (size_t)row * (height + 1);
  uint8_t *pixels = malloc(bytes);
  require(pixels != NULL, "consumer pixel verification allocation");
  memset(pixels, 0xa5, bytes);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, width + 3);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  for (unsigned y = 0; y <= (unsigned)height; y++)
    for (unsigned x = 0; x < row; x++) {
      uint8_t expected = 0xa5;
      if (y < (unsigned)height && x < (unsigned)width * 4) {
        unsigned quadrant = (y >= (unsigned)height / 2) * 2 +
                             (x / 4 >= (unsigned)width / 2);
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
static inline void frame_verify(struct ng_gpu_frame *frame, unsigned number) {
  require(frame != NULL, "owned frame lease");
  require(ng_gpu.server_wait(ng_display, frame->fence, 0),
          "GPU-side wait in unrelated consumer context");
  frame_verify_image(frame->image, frame->width, frame->height, number);
}
