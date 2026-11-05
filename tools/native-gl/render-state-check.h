/* Compare cached values with the driver, including rejected setters, context
 * switches and output bounds. Time the same query workload in both modes. */
#include <time.h>
struct checked_render_state {
  GLint viewport[5], scissor[5], color_mask[5], blend[7], enabled[6];
  GLfloat clear_color[5];
};
static void render_state_read(struct checked_render_state *state) {
  memset(state, 0x5a, sizeof(*state));
  glGetIntegerv(GL_VIEWPORT, state->viewport);
  glGetIntegerv(GL_SCISSOR_BOX, state->scissor);
  glGetIntegerv(GL_COLOR_WRITEMASK, state->color_mask);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, state->clear_color);
  const GLenum blend[] = {GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA,
                          GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB,
                          GL_BLEND_EQUATION_ALPHA};
  for (unsigned i = 0; i < 6; i++)
    glGetIntegerv(blend[i], state->blend + i);
  const GLenum caps[] = {GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST,
                         GL_STENCIL_TEST, GL_CULL_FACE};
  for (unsigned i = 0; i < 5; i++) {
    state->enabled[i] = glIsEnabled(caps[i]);
    GLint integral = -1;
    glGetIntegerv(caps[i], &integral);
    require(integral == state->enabled[i], "integer and boolean enable queries");
  }
}
static void render_state_compare(void) {
  struct checked_render_state cached, driver;
  require(!setenv("IOS_KVM_NATIVE_RENDER_STATE_CACHE", "1", 1), "cache on");
  render_state_read(&cached);
  require(!setenv("IOS_KVM_NATIVE_RENDER_STATE_CACHE", "0", 1), "cache off");
  render_state_read(&driver);
  require(!memcmp(&cached, &driver, sizeof(cached)),
          "cached render state equals driver with output sentinels");
  require(!setenv("IOS_KVM_NATIVE_RENDER_STATE_CACHE", "1", 1), "cache on");
}
static double render_state_seconds(void) {
  struct timespec now;
  require(!clock_gettime(CLOCK_MONOTONIC, &now), "monotonic clock");
  return now.tv_sec + now.tv_nsec / 1e9;
}
static void render_state_changed(uint64_t before) {
  render_state_compare();
  require(ios_kvm_native_gl_state_fetches() == before + 1,
          "setter invalidates its observed state group");
}
static void render_state_check(void) {
  const char *original = getenv("IOS_KVM_NATIVE_RENDER_STATE_CACHE");
  char *saved = original ? strdup(original) : NULL;
  require(!original || saved, "save cache setting");
  struct ios_kvm_native_gl_context *contexts[2];
  for (unsigned i = 0; i < 2; i++) {
    contexts[i] = ios_kvm_native_gl_context_create(3);
    require(contexts[i] && ios_kvm_native_gl_make_current(contexts[i]),
            "render-state context");
    glViewport(3 + i, 7, 257 + i, 131);
    glScissor(-3, 9 + i, 128, 64);
    glColorMask(i, GL_TRUE, GL_FALSE, GL_TRUE);
    glClearColor(-2, 0.25f + i * 0.25f, 3, 1);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_REVERSE_SUBTRACT);
    glEnable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    if (i)
      glEnable(GL_CULL_FACE);
    else
      glDisable(GL_CULL_FACE);
    require(!setenv("IOS_KVM_NATIVE_RENDER_STATE_CACHE", "1", 1), "cache on");
    uint64_t before = ios_kvm_native_gl_state_fetches();
    render_state_compare();
    require(ios_kvm_native_gl_state_fetches() == before + 1,
            "one fused request populates all render state groups");
    require(glGetError() == GL_NO_ERROR, "valid render-state status");
  }
  for (unsigned iteration = 0; iteration < 8; iteration++) {
    unsigned i = iteration & 1;
    require(ios_kvm_native_gl_make_current(contexts[i]), "switch cached state");
    uint64_t before = ios_kvm_native_gl_state_fetches();
    render_state_compare();
    require(ios_kvm_native_gl_state_fetches() == before,
            "context switch preserves its own observed state");
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    require(viewport[0] == (GLint)(3 + i), "context-specific viewport");
    require(glIsEnabled(GL_CULL_FACE) == i, "context-specific capability");
  }
  uint64_t before = ios_kvm_native_gl_state_fetches();
  glViewport(-7, 3, 32767, 32767);
  render_state_changed(before++);
  glScissor(2, -9, 31, 37);
  render_state_changed(before++);
  glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
  render_state_changed(before++);
  glClearColor(2, -1, 0.75f, 0);
  render_state_changed(before++);
  glBlendFunc(GL_ONE, GL_ZERO);
  render_state_changed(before++);
  glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  render_state_changed(before++);
  glBlendEquation(GL_FUNC_SUBTRACT);
  render_state_changed(before++);
  glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
  render_state_changed(before++);
  glEnable(GL_DEPTH_TEST);
  render_state_changed(before++);
  glDisable(GL_DEPTH_TEST);
  render_state_changed(before);
  require(glGetError() == GL_NO_ERROR, "individually invalidated state status");
  /* Rejected mutations invalidate the group without guessing driver results. */
  glViewport(0, 0, -1, 10);
  render_state_compare();
  require(glGetError() == GL_INVALID_VALUE && glGetError() == GL_NO_ERROR,
          "viewport error preserved exactly once across fused query");
  glScissor(0, 0, 10, -1);
  render_state_compare();
  require(glGetError() == GL_INVALID_VALUE, "scissor validation preserved");
  glBlendFunc(0xdeadbeef, GL_ONE);
  render_state_compare();
  require(glGetError() == GL_INVALID_ENUM, "blend function validation preserved");
  glBlendEquation(0xdeadbeef);
  render_state_compare();
  require(glGetError() == GL_INVALID_ENUM, "blend equation validation preserved");
  glEnable(0xdeadbeef);
  render_state_compare();
  require(glGetError() == GL_INVALID_ENUM, "uncached capability error preserved");
  glBlendFunc(GL_ONE, GL_ZERO);
  glBlendEquation(GL_FUNC_SUBTRACT);
  glDisable(GL_BLEND);
  glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
  glClearColor(2, -1, 0.75f, 0);
  render_state_compare();
  require(glGetError() == GL_NO_ERROR, "mutated render-state status");
  struct checked_render_state state;
  double elapsed[2];
  uint64_t fetches = ios_kvm_native_gl_state_fetches();
  for (unsigned mode = 0; mode < 2; mode++) {
    require(!setenv("IOS_KVM_NATIVE_RENDER_STATE_CACHE", mode ? "1" : "0", 1),
            "benchmark cache mode");
    double start = render_state_seconds();
    for (unsigned iteration = 0; iteration < 64; iteration++)
      render_state_read(&state);
    elapsed[mode] = render_state_seconds() - start;
  }
  require(ios_kvm_native_gl_state_fetches() == fetches,
          "unchanged cached queries perform no driver state requests");
  printf("Native render-state query benchmark: uncached %.6f s, cached %.6f s "
         "(64 identical state-query groups)\n",
         elapsed[0], elapsed[1]);
  for (unsigned i = 0; i < 2; i++)
    require(ios_kvm_native_gl_context_destroy(contexts[i]),
            "destroy render-state context");
  if (saved) {
    require(!setenv("IOS_KVM_NATIVE_RENDER_STATE_CACHE", saved, 1),
            "restore cache setting");
    free(saved);
  } else {
    require(!unsetenv("IOS_KVM_NATIVE_RENDER_STATE_CACHE"),
            "restore absent cache setting");
  }
  puts("Native render-state cache PASSED (driver values, invalidation, errors, "
       "context isolation, bounds and request count)");
}
