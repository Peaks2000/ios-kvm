static int ng_render_state_reply(size_t size, void *output, size_t capacity,
                                  size_t *used) {
  if (size)
    return -1;
  struct ng_render_state state = {0};
  ng_errors_collect();
  glGetIntegerv(GL_VIEWPORT, state.viewport);
  glGetIntegerv(GL_SCISSOR_BOX, state.scissor);
  glGetIntegerv(GL_COLOR_WRITEMASK, state.color_mask);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, state.clear_color);
  glGetIntegerv(GL_BLEND_SRC_RGB, state.blend_func);
  glGetIntegerv(GL_BLEND_DST_RGB, state.blend_func + 1);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, state.blend_func + 2);
  glGetIntegerv(GL_BLEND_DST_ALPHA, state.blend_func + 3);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, state.blend_equation);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, state.blend_equation + 1);
  for (unsigned i = 0; i < sizeof(ng_state_caps) / sizeof(*ng_state_caps); i++)
    if (glIsEnabled(ng_state_caps[i]))
      state.enabled |= ng_state_cap_bit(ng_state_caps[i]);
  if (!ng_errors_collect())
    state.valid = NG_STATE_ALL;
  return ng_output(output, capacity, used, &state, sizeof(state));
}
