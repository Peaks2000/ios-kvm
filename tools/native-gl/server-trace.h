/* Bounded, opt-in state evidence for compositor parity investigations. */
static int ng_trace_draws;
static _Thread_local unsigned ng_trace_draw_count;
static _Thread_local size_t ng_trace_shader_bytes;
static _Thread_local unsigned ng_trace_shader_parts, ng_trace_attach_count;
static _Thread_local unsigned ng_trace_frame_count;
static void ng_trace_pixels(GLsizei width, GLsizei height,
                            const uint8_t *pixels, uint64_t cell,
                            uint32_t sequence) {
  if (!ng_trace_draws || width < 750 || height < 1624 ||
      ng_trace_frame_count >= 1024)
    return;
  unsigned frame = ++ng_trace_frame_count;
  if (frame > 3 && frame % 16 > 1)
    return;
  GLint framebuffer = 0, attachment = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &framebuffer);
  if (framebuffer)
    glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &attachment);
  unsigned coloured[4] = {0}, alpha[4] = {0};
  for (GLsizei y = 0; y < height; y++)
    for (GLsizei x = 0; x < width; x++) {
      const uint8_t *pixel = pixels + ((size_t)y * width + x) * 4;
      unsigned quadrant = (y >= height / 2) * 2 + (x >= width / 2);
      coloured[quadrant] += (pixel[0] | pixel[1] | pixel[2]) != 0;
      alpha[quadrant] += pixel[3] != 0;
    }
  fprintf(stderr,
          "native pixels frame=%u cell=%#llx sequence=%u read_fbo=%d attachment=%d size=%dx%d "
          "coloured=%u,%u,%u,%u alpha=%u,%u,%u,%u\n", frame,
          (unsigned long long)cell, sequence, framebuffer,
          attachment, width, height, coloured[0],
          coloured[1], coloured[2], coloured[3], alpha[0], alpha[1], alpha[2],
          alpha[3]);
}
static void ng_trace_shader(GLuint shader, GLsizei count,
                            const GLchar *const *strings, const GLint *lengths) {
  if (!ng_trace_draws)
    return;
  for (GLsizei i = 0; i < count; i++) {
    size_t bytes = (size_t)lengths[i];
    if (ng_trace_shader_parts == 1024 ||
        bytes > (1u << 20) - ng_trace_shader_bytes)
      return;
    ng_trace_shader_parts++;
    ng_trace_shader_bytes += bytes;
    fprintf(stderr, "native shader %u part %d bytes %zu\n", shader, i, bytes);
    fwrite(strings[i], 1, bytes, stderr);
    fputs("\nnative shader part end\n", stderr);
  }
}
static void ng_trace_attach(GLuint program, GLuint shader) {
  if (!ng_trace_draws || ng_trace_attach_count == 256)
    return;
  ng_trace_attach_count++;
  fprintf(stderr, "native attach program=%u shader=%u\n", program, shader);
}
static void ng_trace_draw(const char *operation, GLenum mode, GLint first,
                          GLsizei count) {
  if (!ng_trace_draws || ng_trace_draw_count >= 8192)
    return;
  ng_errors_collect();
  GLint framebuffer = 0, program = 0, viewport[4], scissor[4], mask[4];
  GLint equation[2], stencil_mask = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
  glGetIntegerv(GL_CURRENT_PROGRAM, &program);
  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetIntegerv(GL_SCISSOR_BOX, scissor);
  glGetIntegerv(GL_COLOR_WRITEMASK, mask);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, equation);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, equation + 1);
  glGetIntegerv(GL_STENCIL_WRITEMASK, &stencil_mask);
  fprintf(stderr,
          "native draw %u %s mode=%#x first=%d count=%d fbo=%d program=%d "
          "viewport=%d,%d,%d,%d scissor=%d:%d,%d,%d,%d mask=%d%d%d%d "
          "blend=%d:%#x,%#x depth=%d stencil=%d:%#x\n",
          ++ng_trace_draw_count, operation, mode, first, count, framebuffer,
          program, viewport[0], viewport[1], viewport[2], viewport[3],
          glIsEnabled(GL_SCISSOR_TEST), scissor[0], scissor[1], scissor[2],
          scissor[3], mask[0], mask[1], mask[2], mask[3], glIsEnabled(GL_BLEND),
          equation[0], equation[1], glIsEnabled(GL_DEPTH_TEST),
          glIsEnabled(GL_STENCIL_TEST), stencil_mask);
  ng_errors_collect();
}
