#include "pixel-format.h"
#ifndef NG_FORMAT_SHADER
#define NG_FORMAT_SHADER shader
#endif
struct format_case {
  const char *name;
  GLenum format, internal, type;
  unsigned channels;
};
static void format_pixel(const struct format_case *c, unsigned phase,
                         unsigned x, unsigned y, uint8_t *data,
                         int expected[4]) {
  for (unsigned k = 0; k < 4; k++)
    expected[k] = k == 3 ? 255 : 0;
  if (c->type == GL_UNSIGNED_BYTE) {
    for (unsigned k = 0; k < c->channels; k++)
      expected[k] = data[k] = (phase * 23 + x * 19 + y * 31 + k * 53) & 255;
  } else if (c->type == GL_FLOAT || c->type == GL_HALF_FLOAT ||
             c->type == 0x8d61) {
    const uint16_t halves[] = {0, 0x3400, 0x3800, 0x3a00, 0x3c00};
    for (unsigned k = 0; k < c->channels; k++) {
      unsigned step = (phase + x + y + k) % 5;
      expected[k] = (step * 255 + 2) / 4;
      if (c->type == GL_FLOAT) {
        float value = step / 4.0f;
        memcpy(data + k * 4, &value, 4);
      } else
        memcpy(data + k * 2, &halves[step], 2);
    }
  } else {
    unsigned bits[4] = {5, 6, 5, 0};
    if (c->type == GL_UNSIGNED_SHORT_4_4_4_4)
      bits[0] = bits[1] = bits[2] = bits[3] = 4;
    else if (c->type == GL_UNSIGNED_SHORT_5_5_5_1) {
      bits[1] = 5;
      bits[3] = 1;
    } else if (c->type == GL_UNSIGNED_INT_2_10_10_10_REV) {
      bits[0] = bits[1] = bits[2] = 10;
      bits[3] = 2;
    }
    uint32_t packed = 0;
    unsigned shift = 0;
    for (unsigned k = 0; k < c->channels; k++) {
      unsigned limit = (1u << bits[k]) - 1;
      unsigned value = (phase * 3 + x * 7 + y * 11 + k * 17) & limit;
      expected[k] = (value * 255 + limit / 2) / limit;
      if (c->type == GL_UNSIGNED_INT_2_10_10_10_REV) {
        packed |= value << shift;
        shift += bits[k];
      } else
        packed = (packed << bits[k]) | value;
    }
    memcpy(data, &packed, c->type == GL_UNSIGNED_INT_2_10_10_10_REV ? 4 : 2);
  }
  if (c->format == 0x1906) { /* alpha */
    expected[3] = expected[0];
    expected[0] = expected[1] = expected[2] = 0;
  } else if (c->format == 0x1909 || c->format == 0x190a) {
    if (c->format == 0x190a)
      expected[3] = expected[1];
    expected[1] = expected[2] = expected[0];
  } else if (c->format == NG_BGRA) {
    int red = expected[2];
    expected[2] = expected[0];
    expected[0] = red;
  }
}
static void texture_format_check(void) {
  const struct format_case cases[] = {
      {"alpha8", 0x1906, 0x1906, GL_UNSIGNED_BYTE, 1},
      {"luminance8", 0x1909, 0x1909, GL_UNSIGNED_BYTE, 1},
      {"luminance-alpha8", 0x190a, 0x190a, GL_UNSIGNED_BYTE, 2},
      {"red8", GL_RED, GL_R8, GL_UNSIGNED_BYTE, 1},
      {"rg8", GL_RG, GL_RG8, GL_UNSIGNED_BYTE, 2},
      {"rgb8", GL_RGB, GL_RGB8, GL_UNSIGNED_BYTE, 3},
      {"rgba8", GL_RGBA, GL_RGBA8, GL_UNSIGNED_BYTE, 4},
      {"bgra8", NG_BGRA, NG_BGRA, GL_UNSIGNED_BYTE, 4},
      {"red16f", GL_RED, GL_R16F, GL_HALF_FLOAT, 1},
      {"rg16f", GL_RG, GL_RG16F, GL_HALF_FLOAT, 2},
      {"rgb16f", GL_RGB, GL_RGB16F, GL_HALF_FLOAT, 3},
      {"rgba16f", GL_RGBA, GL_RGBA16F, GL_HALF_FLOAT, 4},
      {"rgba16f-oes-token", GL_RGBA, GL_RGBA16F, 0x8d61, 4},
      {"red32f", GL_RED, GL_R32F, GL_FLOAT, 1},
      {"rg32f", GL_RG, GL_RG32F, GL_FLOAT, 2},
      {"rgb32f", GL_RGB, GL_RGB32F, GL_FLOAT, 3},
      {"rgba32f", GL_RGBA, GL_RGBA32F, GL_FLOAT, 4},
      {"rgb565", GL_RGB, GL_RGB565, GL_UNSIGNED_SHORT_5_6_5, 3},
      {"rgba4444", GL_RGBA, GL_RGBA4, GL_UNSIGNED_SHORT_4_4_4_4, 4},
      {"rgba5551", GL_RGBA, GL_RGB5_A1, GL_UNSIGNED_SHORT_5_5_5_1, 4},
      {"rgba2101010", GL_RGBA, GL_RGB10_A2, GL_UNSIGNED_INT_2_10_10_10_REV, 4},
  };
  GLuint
      vs = NG_FORMAT_SHADER(
          GL_VERTEX_SHADER,
          "layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);}"),
      fs = NG_FORMAT_SHADER(
          GL_FRAGMENT_SHADER,
          "precision highp float;uniform sampler2D image;out vec4 pixel;void "
          "main(){pixel=texelFetch(image,ivec2(gl_FragCoord.xy),0);}");
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint linked, previous;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  require(linked, "format shader link");
  glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "image"), 0);
  GLuint output = texture(5, 3), input, fbo;
  glGenTextures(1, &input);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         output, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "format output FBO");
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, input);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glViewport(0, 0, 5, 3);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 7);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
  glPixelStorei(GL_PACK_ALIGNMENT, 8);
  glPixelStorei(GL_PACK_ROW_LENGTH, 9);
  glPixelStorei(GL_PACK_SKIP_ROWS, 1);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 2);
  for (unsigned n = 0; n < sizeof(cases) / sizeof(cases[0]); n++) {
    const struct format_case *c = &cases[n];
    size_t pixel = ng_pixel_size(c->format, c->type), stride = (pixel * 7 + 7) & ~(size_t)7;
    require(pixel && stride * 4 <= 1024, "format fixture bounds");
    for (unsigned frame = 0; frame < 3; frame++) {
      uint8_t source[1024], result[256], expected[256], scratch[16];
      int color[4];
      memset(source, 0x71, sizeof(source));
      memset(result, 0xa5, sizeof(result));
      memset(expected, 0xa5, sizeof(expected));
      for (unsigned y = 0; y < 3; y++)
        for (unsigned x = 0; x < 5; x++)
          format_pixel(c, frame, x, y,
                       source + (y + 1) * stride + (x + 1) * pixel, color);
      glTexImage2D(GL_TEXTURE_2D, 0, c->internal, 5, 3, 0, c->format, c->type,
                   source);
      for (unsigned x = 0; x < 2; x++)
        format_pixel(c, frame + 1, x, 0, source + stride + (x + 1) * pixel,
                     color);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 2, 1, c->format, c->type, source);
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL);
      glReadPixels(0, 0, 5, 3, GL_RGBA, GL_UNSIGNED_BYTE, result);
      for (unsigned y = 0; y < 3; y++)
        for (unsigned x = 0; x < 5; x++) {
          int patched = y == 1 && x >= 1 && x <= 2;
          format_pixel(c, frame + patched, patched ? x - 1 : x, patched ? 0 : y,
                       scratch, color);
          for (unsigned k = 0; k < 4; k++)
            expected[40 * (y + 1) + 8 + x * 4 + k] = color[k];
        }
      for (unsigned j = 0; j < sizeof(result); j++) {
        unsigned y = j / 40, x = j % 40;
        int active = y >= 1 && y <= 3 && x >= 8 && x < 28;
        if (active ? abs((int)result[j] - expected[j]) > 1
                   : result[j] != expected[j]) {
          fprintf(stderr, "format %s frame %u byte %u: got %u expected %u\n",
                  c->name, frame, j, result[j], expected[j]);
          require(0, "format sampled pixels/padding");
        }
      }
      GLenum error = glGetError();
      if (error)
        fprintf(stderr, "format %s GL error %#x\n", c->name, error);
      require(error == 0, "format GL status");
    }
    printf("Native texture format %s: 3 fresh padded image/subimage samples "
           "passed\n",
           c->name);
  }
  GLuint incomplete;
  glGenFramebuffers(1, &incomplete);
  glBindFramebuffer(GL_FRAMEBUFFER, incomplete);
  uint8_t untouched[256];
  memset(untouched, 0xa5, sizeof(untouched));
  glReadPixels(0, 0, 5, 3, GL_RGBA, GL_UNSIGNED_BYTE, untouched);
  for (unsigned j = 0; j < sizeof(untouched); j++)
    require(untouched[j] == 0xa5,
            "copied GPU read error preserves destination");
  require(glGetError() == GL_INVALID_FRAMEBUFFER_OPERATION && glGetError() == 0,
          "copied GPU error preserved once");
  glDeleteFramebuffers(1, &incomplete);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &input);
  glDeleteTextures(1, &output);
  glUseProgram(previous);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  puts("Native texture format check PASSED (21 formats, 63 sampled frames, "
       "copied read error)");
}
