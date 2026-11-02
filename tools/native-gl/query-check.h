/* Real query values, output sentinels and relink invalidation. */
static void query_uniform_cache(GLuint restore_program) {
  GLuint vs = shader(GL_VERTEX_SHADER,
                     "in vec2 p;void main(){gl_Position=vec4(p,0,1);}");
  GLuint fs = shader(GL_FRAGMENT_SHADER,
                     "precision highp float;uniform vec4 colors[2];out vec4 "
                     "pixel;void main(){pixel=colors[0]+colors[1];}");
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  require(ok, "query program link");
  glUseProgram(program);
  GLint first = glGetUniformLocation(program, "colors[0]"),
        second = glGetUniformLocation(program, "colors[1]");
  require(first >= 0 && second >= 0, "array uniform locations");
  const GLfloat four[] = {1, 2, 3, 4, 5, 6, 7, 8};
  glUniform4fv(first, 2, four);
  GLfloat value[5] = {0, 0, 0, 0, 12345};
  glGetUniformfv(program, second, value);
  for (unsigned i = 0; i < 4; i++)
    require(value[i] == four[i + 4], "uniform array element");
  require(value[4] == 12345, "vec4 query output bound");
  glDetachShader(program, fs);
  glDeleteShader(fs);
  fs = shader(GL_FRAGMENT_SHADER,
              "precision highp float;uniform vec2 colors[2];out vec4 "
              "pixel;void main(){pixel=vec4(colors[0]+colors[1],0,1);}");
  glAttachShader(program, fs);
  glLinkProgram(program);
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  require(ok, "query program relink");
  glUseProgram(program);
  first = glGetUniformLocation(program, "colors[0]");
  second = glGetUniformLocation(program, "colors[1]");
  const GLfloat two[] = {9, 10, 11, 12};
  glUniform2fv(first, 2, two);
  GLfloat small[] = {0, 0, 54321};
  glGetUniformfv(program, second, small);
  require(small[0] == 11 && small[1] == 12 && small[2] == 54321,
          "relink invalidates uniform component cache");
  GLint integral[] = {0, 0, 76543};
  glGetUniformiv(program, second, integral);
  require(integral[0] == 11 && integral[1] == 12 && integral[2] == 76543,
          "integer uniform query bound");
  glUseProgram(restore_program);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  require(glGetError() == 0, "uniform cache query GL status");
}
static void query_program(GLuint program, GLuint vertex, GLuint fragment,
                          GLint matrix, GLint tint, GLint sampler) {
  GLuint attached[3] = {0, 0, 0x12345678};
  GLsizei count = -1;
  glGetAttachedShaders(program, 2, &count, attached);
  require(count == 2 && attached[2] == 0x12345678 &&
              ((attached[0] == vertex && attached[1] == fragment) ||
               (attached[0] == fragment && attached[1] == vertex)),
          "attached shader identities/bounds");
  attached[0] = 0x12345678;
  glGetAttachedShaders(program, 0, &count, attached);
  require(count == 0 && attached[0] == 0x12345678,
          "zero-capacity attached shader query");
  GLint uniforms = 0;
  glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &uniforms);
  require(uniforms == 3, "active uniform count");
  for (GLint i = 0; i < uniforms; i++) {
    unsigned char name[128];
    memset(name, 0xa5, sizeof(name));
    GLsizei length = -1;
    GLint size = -1;
    GLenum type = 0;
    glGetActiveUniform(program, i, 127, &length, &size, &type, (char *)name);
    require(length > 0 && length < 127 && name[length] == 0 &&
                name[length + 1] == 0xa5 && size == 1,
            "active uniform name/metadata bound");
    require((!strcmp((char *)name, "transform") && type == GL_FLOAT_MAT4) ||
                (!strcmp((char *)name, "tint") && type == GL_FLOAT_VEC4) ||
                (!strcmp((char *)name, "image") && type == GL_SAMPLER_2D),
            "active uniform type");
    unsigned char truncated[2] = {0xa5, 0xa5};
    glGetActiveUniform(program, i, 1, &length, &size, &type, (char *)truncated);
    require(length == 0 && truncated[0] == 0 && truncated[1] == 0xa5,
            "truncated uniform name bound");
    truncated[0] = 0xa5;
    glGetActiveUniform(program, i, 0, &length, &size, &type, (char *)truncated);
    require(length == 0 && truncated[0] == 0xa5, "zero-capacity uniform name");
  }
  GLfloat mat[17];
  for (unsigned i = 0; i < 17; i++)
    mat[i] = 12345;
  glGetUniformfv(program, matrix, mat);
  for (unsigned i = 0; i < 16; i++)
    require(mat[i] == (i % 5 == 0 ? 1 : 0), "matrix uniform query");
  require(mat[16] == 12345, "matrix query output bound");
  GLfloat color[5] = {0, 0, 0, 0, 54321};
  glGetUniformfv(program, tint, color);
  require(color[0] == 1 && color[1] == 1 && color[2] == 1 && color[3] == 1 &&
              color[4] == 54321,
          "vec4 uniform query");
  GLint unit[2] = {-1, 76543};
  glGetUniformiv(program, sampler, unit);
  require(unit[0] == 0 && unit[1] == 76543, "sampler query bound");
  glClearColor(.25f, .5f, .75f, 1);
  GLfloat clear[5] = {0, 0, 0, 0, 98765};
  glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
  require(clear[0] == .25f && clear[1] == .5f && clear[2] == .75f &&
              clear[3] == 1 && clear[4] == 98765,
          "vector float query");
  query_uniform_cache(program);
}
static void query_geometry(GLuint output) {
  GLint params[5] = {-1, -1, -1, -1, 12345};
  glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                        params);
  require((GLuint)params[0] == output && params[1] == -1,
          "framebuffer attachment query");
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, params);
  require(params[0] == GL_NEAREST && params[1] == -1,
          "texture parameter query");
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, params);
  require(params[0] == GL_TRUE && params[1] == -1, "vertex attribute query");
  glVertexAttrib4f(3, 2, 3, 4, 1);
  glGetVertexAttribiv(3, GL_CURRENT_VERTEX_ATTRIB, params);
  require(params[0] == 2 && params[1] == 3 && params[2] == 4 &&
              params[3] == 1 && params[4] == 12345,
          "vector attribute query bound");
  require(glGetError() == 0, "state query GL status");
}
static void compressed_pixels(void) {
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  GLuint output = texture(7, 5), fbo;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         output, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "compressed output FBO");
  GLuint input = texture(7, 5);
  glViewport(0, 0, 7, 5);
  for (unsigned frame = 0; frame < 8; frame++) {
    uint8_t blocks[32] = {0};
    for (unsigned by = 0; by < 2; by++)
      for (unsigned bx = 0; bx < 2; bx++) {
        unsigned red = (frame + bx + 1) % 12 + 1, green = by + 3;
        uint8_t *b = blocks + (by * 2 + bx) * 8;
        b[0] = red * 17;
        b[1] = green * 17;
        b[2] = 0x77;
      }
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB8_ETC2, 7, 5, 0,
                           sizeof(blocks), blocks);
    GLint width = 0, height = 0, compressed = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED,
                             &compressed);
    require(width == 7 && height == 5 && compressed == GL_TRUE,
            "compressed texture metadata");
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL);
    uint8_t pixels[7 * 5 * 4];
    glReadPixels(0, 0, 7, 5, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (unsigned y = 0; y < 5; y++)
      for (unsigned x = 0; x < 7; x++) {
        int expected[] = {((frame + x / 4 + 1) % 12 + 1) * 17 + 2,
                          (y / 4 + 3) * 17 + 2, 121, 255};
        for (unsigned c = 0; c < 4; c++)
          require(abs((int)pixels[(y * 7 + x) * 4 + c] - expected[c]) <= 1,
                  "native ETC2 decoded pixels");
      }
    require(glGetError() == 0, "compressed texture GL status");
  }
  glDeleteTextures(1, &input);
  glDeleteTextures(1, &output);
  glDeleteFramebuffers(1, &fbo);
  puts("Native proxy queries and eight spatial ETC2 frames passed");
}
