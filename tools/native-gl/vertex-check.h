#ifndef NG_VERTEX_SHADER
#define NG_VERTEX_SHADER shader
#endif
static void client_vertex_check(void) {
  struct vertex {
    GLfloat position[2];
    GLubyte colour[4], padding[4];
  } interleaved[8];
  GLubyte colours[8][4];
  GLfloat positions[8][2] = {{-3, -3}, {-3, -3}, {-3, -3}, {-3, -3},
                             {-1, -1}, {1, -1},  {-1, 1},  {1, 1}};
  for (unsigned i = 0; i < 8; i++) {
    memcpy(interleaved[i].position, positions[i], sizeof(positions[i]));
    memset(interleaved[i].padding, 0x71, 4);
  }
  GLint previous_vao, previous_array, previous_program;
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_array);
  glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
  GLuint vs =
             NG_VERTEX_SHADER(GL_VERTEX_SHADER,
                              "layout(location=0) in vec2 p;layout(location=1) "
                              "in vec4 c;out vec4 colour;void "
                              "main(){gl_Position=vec4(p,0,1);colour=c;colour."
                              "r*=gl_VertexID>=4?1.:0.;}"),
         fs = NG_VERTEX_SHADER(GL_FRAGMENT_SHADER,
                               "precision highp float;in vec4 colour;out vec4 "
                               "pixel;void main(){pixel=colour;}");
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  require(linked, "client vertex shader link");
  glUseProgram(program);
  GLuint vaos[2], buffers[2], output = texture(5, 3), fbo;
  glGenVertexArrays(2, vaos);
  glGenBuffers(2, buffers);
  for (unsigned i = 0; i < 2; i++) {
    glBindVertexArray(vaos[i]);
    if (i) {
      glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
      glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions,
                   GL_STATIC_DRAW);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    } else {
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(interleaved[0]),
                            interleaved);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer(
        1, 4, GL_UNSIGNED_BYTE, GL_TRUE, i ? 0 : sizeof(interleaved[0]),
        i ? (const void *)colours : (const void *)interleaved[0].colour);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
  }
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         output, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "client vertex FBO");
  glViewport(0, 0, 5, 3);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  const GLubyte indices8[] = {4, 5, 6, 6, 5, 7};
  const GLushort indices16[] = {4, 5, 6, 6, 5, 7};
  const GLuint indices32[] = {4, 5, 6, 6, 5, 7};
  const GLushort restart[] = {4, 5, 6, 7, 0xffff, 4, 5, 6, 7};
  for (unsigned frame = 0; frame < 12; frame++) {
    unsigned which = frame & 1, kind = frame % 6;
    glBindVertexArray(vaos[which]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    GLubyte expected[] = {31 + frame * 11, 183 - frame * 7, 77 + frame * 5,
                          255};
    for (unsigned i = 0; i < 8; i++) {
      memcpy(colours[i], expected, 4);
      memcpy(interleaved[i].colour, expected, 4);
    }
    GLint binding = -1, stride = -1, normalized = -1;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &binding);
    require(binding == (which ? (GLint)buffers[0] : 0),
            "public vertex buffer identity");
    glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
    glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &normalized);
    require(stride == (which ? 0 : (GLint)sizeof(interleaved[0])) && normalized,
            "client attribute metadata");
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (kind == 0)
      glDrawArrays(GL_TRIANGLE_STRIP, 4, 4);
    else if (kind == 1)
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices8);
    else if (kind == 2)
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices16);
    else if (kind == 3) {
      GLushort storage[8] = {0x7171, 0x7171, 4, 5, 6, 6, 5, 7};
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(storage), storage,
                   GL_STREAM_DRAW);
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void *)4);
    } else if (kind == 4)
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, indices32);
    else {
      glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
      glDrawElements(GL_TRIANGLE_STRIP, 9, GL_UNSIGNED_SHORT, restart);
      glDisable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
    }
    /* No query/flush intervenes: queued draws must own the bytes already. */
    memset(colours, 0, sizeof(colours));
    for (unsigned i = 0; i < 8; i++)
      memset(interleaved[i].colour, 0, 4);
    void *pointer = NULL;
    glGetVertexAttribPointerv(1, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
    require(pointer ==
                (which ? (void *)colours : (void *)interleaved[0].colour),
            "guest pointer identity stays local");
    GLubyte pixels[5 * 3 * 4];
    memset(pixels, 0xa5, sizeof(pixels));
    glReadPixels(0, 0, 5, 3, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (unsigned j = 0; j < sizeof(pixels); j++)
      require(abs((int)pixels[j] - expected[j % 4]) <= 1,
              "fresh client snapshot rendered pixels");
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &binding);
    require(binding == (kind == 3 ? (GLint)buffers[1] : 0),
            "private index buffer restored");
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &binding);
    require(binding == 0, "private vertex buffer restored");
    require(glGetError() == 0, "client vertex GL status");
  }
  glBindVertexArray(vaos[0]);
  glVertexAttribPointer(0, 0, GL_FLOAT, GL_FALSE, 0, NULL);
  require(glGetError() == GL_INVALID_VALUE && glGetError() == 0,
          "invalid client attribute error once");
  void *pointer = NULL;
  glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
  require(pointer == interleaved,
          "invalid pointer definition preserves old state");
  glBindVertexArray(previous_vao);
  glBindBuffer(GL_ARRAY_BUFFER, previous_array);
  glDeleteVertexArrays(2, vaos);
  glDeleteBuffers(2, buffers);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &output);
  glUseProgram(previous_program);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  require(glGetError() == 0, "client vertex cleanup");
  puts("Native client vertices PASSED (12 frames, fresh snapshots, "
       "interleaving, normalized colours, VAOs, array/byte/short/int/EBO "
       "draws, restart, local pointer queries)");
}
