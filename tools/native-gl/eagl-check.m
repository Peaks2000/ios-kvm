/* Isolated test of the same private EAGL macro table used by CoreAnimation. */
#include <GLES3/gl3.h>
#import <OpenGLES/EAGL.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
@interface EAGLContext (NativeMacroCheck)
- (void *)getMacroContextPrivate;
@end
static void **ng_eagl_table;
#include "eagl-test-dispatch.h"
static void check(int ok, const char *why) {
  if (!ok) {
    fprintf(stderr, "Native EAGL check FAILED: %s\n", why);
    exit(1);
  }
}
static GLuint shader(GLenum kind, const char *text) {
  GLuint s = glCreateShader(kind);
  glShaderSource(s, 1, &text, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(s, sizeof(log), NULL, log);
    fprintf(stderr, "%s\n", log);
  }
  check(ok, "shader compilation");
  return s;
}
int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc != 2)
      return 1;
    (void)[EAGLContext class];
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
      fprintf(stderr, "%s\n", dlerror());
      return 1;
    }
    EAGLContext *context =
        [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    check(context != nil, "EAGL context init");
    ng_eagl_table = [context getMacroContextPrivate];
    check(ng_eagl_table && ng_eagl_table[0], "macro context table");
    printf("Native EAGL macro renderer: %s\n", glGetString(GL_RENDERER));
    const GLfloat p[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    const GLushort indices[] = {0, 1, 2, 2, 1, 3};
    GLuint vao, buffers[2];
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(2, buffers);
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(p), p, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                 GL_STATIC_DRAW);
    GLuint texture, fbo;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 17, 13, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture, 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "framebuffer");
    glViewport(0, 0, 17, 13);
    for (unsigned variant = 0; variant < 3; variant++) {
      const char *vertex =
          variant == 0 ? "/* modern shader */\n#version 300 es\nin vec2 p;void "
                         "main(){gl_Position=vec4(p,0,1);}"
          : variant == 1
              ? "#version 100\nattribute vec2 p;void "
                "main(){gl_Position=vec4(p,0,1);}"
              : "attribute vec2 p;void main(){gl_Position=vec4(p,0,1);}";
      const char *fragment =
          variant == 0 ? "#version 300 es\nprecision highp float;uniform vec4 "
                         "color;out vec4 pixel;void main(){pixel=color;}"
          : variant == 1
              ? "// legacy shader\n#version 100\nprecision highp float;uniform "
                "vec4 color;void main(){gl_FragColor=color;}"
              : "precision highp float;uniform vec4 color;void "
                "main(){gl_FragColor=color;}";
      GLuint vs = shader(GL_VERTEX_SHADER, vertex),
             fs = shader(GL_FRAGMENT_SHADER, fragment),
             program = glCreateProgram();
      glAttachShader(program, vs);
      glAttachShader(program, fs);
      glBindAttribLocation(program, 0, "p");
      glLinkProgram(program);
      GLint linked = 0;
      glGetProgramiv(program, GL_LINK_STATUS, &linked);
      check(linked, "program link");
      glUseProgram(program);
      GLint color = glGetUniformLocation(program, "color");
      check(color >= 0, "uniform location");
      for (unsigned frame = 0; frame < 8; frame++) {
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        for (unsigned half = 0; half < 2; half++) {
          glScissor(half ? 8 : 0, 0, half ? 9 : 8, 13);
          GLfloat rgba[] = {(frame + 1) / 16.0f, half ? .75f : .25f, .5f, 1};
          glUniform4fv(color, 1, rgba);
          glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL);
        }
        GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        check(fence != NULL, "EAGL fence");
        GLenum wait =
            glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
        check(wait == GL_ALREADY_SIGNALED || wait == GL_CONDITION_SATISFIED,
              "EAGL fence completion");
        glDeleteSync(fence);
        glFinish();
        uint8_t pixels[17 * 13 * 4];
        glReadPixels(0, 0, 17, 13, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        for (unsigned y = 0; y < 13; y++)
          for (unsigned x = 0; x < 17; x++) {
            int want[] = {((frame + 1) * 255 + 8) / 16, x < 8 ? 64 : 191, 128,
                          255};
            for (unsigned k = 0; k < 4; k++)
              check(abs((int)pixels[(y * 17 + x) * 4 + k] - want[k]) <= 1,
                    "EAGL macro rendered pixels");
          }
        check(glGetError() == 0, "EAGL GL error state");
        printf("Native EAGL macro frame %u: all pixels passed\n", frame);
      }
      glDeleteProgram(program);
      glDeleteShader(vs);
      glDeleteShader(fs);
      printf("Native EAGL shader variant %u passed\n", variant);
    }
    glDeleteBuffers(2, buffers);
    glDeleteVertexArrays(1, &vao);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &texture);
    glFinish();
    check(glGetError() == 0, "EAGL cleanup");
    [context release];
    ng_eagl_table = NULL;
    puts("Native EAGL macro ABI check PASSED (24 spatial frames, legacy/modern "
         "shaders and real "
         "fences)");
  }
  return 0;
}
