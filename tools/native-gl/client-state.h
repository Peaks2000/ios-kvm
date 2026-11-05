/* Only a driver response populates the cache. Setters invalidate even when
 * their arguments may be rejected, leaving clamping and validation to GL. */
static _Atomic uint64_t ng_state_fetches;
uint64_t ios_kvm_native_gl_state_fetches(void) {
  return atomic_load_explicit(&ng_state_fetches, memory_order_relaxed);
}
static int ng_state_available(unsigned group) {
  const char *setting = getenv("IOS_KVM_NATIVE_RENDER_STATE_CACHE");
  if (setting && !strcmp(setting, "0"))
    return 0;
  struct ng_render_state *state = &ng_current()->render_state;
  if (!(state->valid & group)) {
    ng_call(NG_GET_RENDER_STATE, NULL, 0, state, sizeof(*state));
    uint64_t previous = atomic_fetch_add_explicit(&ng_state_fetches, 1,
                                                 memory_order_relaxed);
    if (state->valid & ~NG_STATE_ALL)
      ng_failure("invalid render state response");
    const char *trace = getenv("IOS_KVM_NATIVE_TRACE_ERRORS");
    if (!previous && state->valid && trace && !strcmp(trace, "1"))
      fprintf(stderr, "native GLES fused render state cache active\n");
  }
  return !!(state->valid & group);
}
static void ng_state_invalidate(unsigned group) {
  ng_current()->render_state.valid &= ~group;
}
static int ng_state_integer(GLenum pname, GLint *params) {
  struct ng_render_state *state = &ng_current()->render_state;
  const int32_t *value;
  unsigned group, count = 1;
  switch (pname) {
  case GL_VIEWPORT:
    group = NG_STATE_VIEWPORT;
    value = state->viewport;
    count = 4;
    break;
  case GL_SCISSOR_BOX:
    group = NG_STATE_SCISSOR;
    value = state->scissor;
    count = 4;
    break;
  case GL_COLOR_WRITEMASK:
    group = NG_STATE_COLOR_MASK;
    value = state->color_mask;
    count = 4;
    break;
  case GL_BLEND_SRC_RGB:
    group = NG_STATE_BLEND_FUNC;
    value = state->blend_func;
    break;
  case GL_BLEND_DST_RGB:
    group = NG_STATE_BLEND_FUNC;
    value = state->blend_func + 1;
    break;
  case GL_BLEND_SRC_ALPHA:
    group = NG_STATE_BLEND_FUNC;
    value = state->blend_func + 2;
    break;
  case GL_BLEND_DST_ALPHA:
    group = NG_STATE_BLEND_FUNC;
    value = state->blend_func + 3;
    break;
  case GL_BLEND_EQUATION_RGB:
  case GL_BLEND_EQUATION_ALPHA:
    group = NG_STATE_BLEND_EQUATION;
    value = state->blend_equation + (pname == GL_BLEND_EQUATION_ALPHA);
    break;
  default: {
    unsigned bit = ng_state_cap_bit(pname);
    if (!bit || !ng_state_available(NG_STATE_CAPS))
      return 0;
    if (!params)
      ng_failure("integer query");
    *params = !!(state->enabled & bit);
    return 1;
  }
  }
  if (!ng_state_available(group))
    return 0;
  if (!params)
    ng_failure("integer query");
  memcpy(params, value, count * sizeof(*params));
  return 1;
}
static int ng_state_float(GLenum pname, GLfloat *params) {
  if (pname != GL_COLOR_CLEAR_VALUE || !ng_state_available(NG_STATE_CLEAR_COLOR))
    return 0;
  if (!params)
    ng_failure("float query");
  memcpy(params, ng_current()->render_state.clear_color, 4 * sizeof(*params));
  return 1;
}
static int ng_state_enabled(GLenum cap, GLboolean *value) {
  unsigned bit = ng_state_cap_bit(cap);
  if (!bit || !ng_state_available(NG_STATE_CAPS))
    return 0;
  *value = !!(ng_current()->render_state.enabled & bit);
  return 1;
}
