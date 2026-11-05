#include "labels.h"
static void debug_label_check(void) {
  GLint old_texture, old_buffer, old_vao;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_buffer);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);
  GLuint texture_name, buffer, vao,
      shader_name = glCreateShader(GL_VERTEX_SHADER),
      program = glCreateProgram();
  glGenTextures(1, &texture_name);
  glBindTexture(GL_TEXTURE_2D, texture_name);
  glGenBuffers(1, &buffer);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glBufferData(GL_ARRAY_BUFFER, 8, NULL, GL_STATIC_DRAW);
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  const GLenum types[] = {0x1702, 0x9151, 0x9154, 0x8b48, 0x8b40};
  const GLuint names[] = {texture_name, buffer, vao, shader_name, program};
  for (unsigned i = 0; i < 5; i++) {
    char input[1030], output[1040];
    for (unsigned j = 0; j < sizeof(input) - 1; j++)
      input[j] = 'A' + (j + i) % 26;
    input[sizeof(input) - 1] = 0;
    glLabelObjectEXT(types[i], names[i], 0, input);
    GLsizei length = -1;
    memset(output, 0x71, sizeof(output));
    glGetObjectLabelEXT(types[i], names[i], sizeof(output), &length, output);
    require(length == (GLsizei)strlen(input) &&
                !memcmp(output, input, strlen(input) + 1),
            "full EXT debug label survives native length cap");
    require((unsigned char)output[length + 1] == 0x71,
            "label query untouched suffix");
    memset(output, 0x71, sizeof(output));
    length = -1;
    glGetObjectLabelEXT(types[i], names[i], 4, &length, output);
    require(length == 3 && !memcmp(output, input, 3) && output[3] == 0 &&
                output[4] == 0x71,
            "truncated label terminator and bounds");
    length = -1;
    glGetObjectLabelEXT(types[i], names[i], 0, &length, NULL);
    require(length == (GLsizei)strlen(input), "length-only label query");
    length = -1;
    output[0] = 0x71;
    glGetObjectLabelEXT(types[i], names[i], 0, &length, output);
    require(length == 0 && output[0] == 0x71, "zero-capacity label storage");
    const char binary[] = {'a', 'b', 0, 'd', 'e'};
    glLabelObjectEXT(types[i], names[i], sizeof(binary), binary);
    memset(output, 0x71, sizeof(output));
    glGetObjectLabelEXT(types[i], names[i], sizeof(output), &length, output);
    require(length == 5 && !memcmp(output, binary, 5) && output[5] == 0 &&
                output[6] == 0x71,
            "explicit label byte count");
    glLabelObjectEXT(types[i], names[i], 123, NULL);
    output[0] = 0x71;
    glGetObjectLabelEXT(types[i], names[i], sizeof(output), &length, output);
    require(length == 0 && output[0] == 0, "null label removes metadata");
    require(glGetError() == 0, "native object label GL status");
  }
  char unchanged[8];
  memset(unchanged, 0x71, sizeof(unchanged));
  GLsizei length = 77;
  glGetObjectLabelEXT(0x1702, 0, sizeof(unchanged), &length, unchanged);
  require(length == 77 && unchanged[0] == 0x71 &&
              glGetError() == GL_INVALID_OPERATION,
          "invalid object preserves output");
  glLabelObjectEXT(0xdeadbeef, texture_name, 0, "invalid");
  require(glGetError() == GL_INVALID_ENUM, "invalid label object type");
  glLabelObjectEXT(0x1702, texture_name, -1, "invalid");
  require(glGetError() == GL_INVALID_VALUE && glGetError() == 0,
          "negative label length error once");
  glLabelObjectEXT(0x1702, texture_name, 0, "deleted");
  glDeleteTextures(1, &texture_name);
  glGetObjectLabelEXT(0x1702, texture_name, sizeof(unchanged), &length,
                      unchanged);
  require(glGetError() == GL_INVALID_OPERATION,
          "deleted label object rejected");
  glGenTextures(1, &texture_name);
  glBindTexture(GL_TEXTURE_2D, texture_name);
  glGetObjectLabelEXT(0x1702, texture_name, sizeof(unchanged), &length,
                      unchanged);
  require(length == 0 && unchanged[0] == 0, "new object starts unlabeled");
  glBindTexture(GL_TEXTURE_2D, old_texture);
  glBindBuffer(GL_ARRAY_BUFFER, old_buffer);
  glBindVertexArray(old_vao);
  glDeleteTextures(1, &texture_name);
  glDeleteBuffers(1, &buffer);
  glDeleteVertexArrays(1, &vao);
  glDeleteShader(shader_name);
  glDeleteProgram(program);
  require(glGetError() == 0, "debug label cleanup");
  puts("Native debug labels PASSED (5 object types, "
       "long/truncated/explicit/removed labels, output bounds, lifetime and "
       "errors)");
}
