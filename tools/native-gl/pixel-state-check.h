/* Exercise independent pack/unpack state through actual texture transfers,
 * across explicit context switches. Queries still come from the host driver. */
static void pixel_state_context_check(void) {
  struct ios_kvm_native_gl_context *contexts[2];
  GLuint textures[2], fbos[2];
  for (unsigned i = 0; i < 2; i++) {
    contexts[i] = ios_kvm_native_gl_context_create(3);
    require(contexts[i] && ios_kvm_native_gl_make_current(contexts[i]),
            "pixel-state context");
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(GL_PACK_ROW_LENGTH, 4 + i * 4);
    glPixelStorei(GL_PACK_SKIP_ROWS, 1);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 6 + i);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
    textures[i] = texture(2, 2);
    glGenFramebuffers(1, &fbos[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           textures[i], 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "pixel-state framebuffer");
  }
  for (unsigned iteration = 0; iteration < 8; iteration++) {
    unsigned i = iteration & 1, unpack_row = (6 + i) * 4,
             pack_row = (4 + i * 4) * 4;
    require(ios_kvm_native_gl_make_current(contexts[i]),
            "pixel-state switching");
    uint8_t input[160], output[160], expected[160];
    memset(input, 0x71, sizeof(input));
    memset(output, 0xa5, sizeof(output));
    memset(expected, 0xa5, sizeof(expected));
    for (unsigned y = 0; y < 2; y++)
      for (unsigned x = 0; x < 2; x++) {
        uint8_t color[] = {iteration * 13 + x, i * 71 + y, 173, 255};
        memcpy(input + (2 + y) * unpack_row + 8 + x * 4, color, 4);
        memcpy(expected + (1 + y) * pack_row + 4 + x * 4, color, 4);
      }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE,
                    input);
    glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, output);
    require(!memcmp(output, expected, sizeof(output)),
            "context-owned pack/unpack pixels and padding");
    GLint actual = -1;
    glGetIntegerv(GL_PACK_ROW_LENGTH, &actual);
    require(actual == (GLint)(4 + i * 4), "driver pack state matches transfer");
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &actual);
    require(actual == 2, "driver unpack state matches transfer");
    require(glGetError() == GL_NO_ERROR, "pixel-state GL status");
  }
  for (unsigned i = 0; i < 2; i++) {
    require(ios_kvm_native_gl_make_current(contexts[i]),
            "pixel-state cleanup bind");
    glDeleteFramebuffers(1, &fbos[i]);
    glDeleteTextures(1, &textures[i]);
    require(ios_kvm_native_gl_context_destroy(contexts[i]),
            "pixel-state destroy");
  }
  puts("Native pixel-state contexts PASSED (independent packing, uploads, "
       "switches, driver queries)");
}
