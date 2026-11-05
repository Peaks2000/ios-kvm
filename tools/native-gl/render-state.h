/* Driver-observed context state shared by the authenticated wire peers. */
#ifndef IOS_KVM_NATIVE_GL_RENDER_STATE_H
#define IOS_KVM_NATIVE_GL_RENDER_STATE_H
#include <stdint.h>
enum {
  NG_STATE_CAPS = 1,
  NG_STATE_VIEWPORT = 2,
  NG_STATE_SCISSOR = 4,
  NG_STATE_COLOR_MASK = 8,
  NG_STATE_CLEAR_COLOR = 16,
  NG_STATE_BLEND_FUNC = 32,
  NG_STATE_BLEND_EQUATION = 64,
  NG_STATE_ALL = 127
};
struct ng_render_state {
  uint32_t valid;
  int32_t viewport[4], scissor[4], color_mask[4];
  float clear_color[4];
  int32_t blend_func[4], blend_equation[2];
  uint32_t enabled;
};
_Static_assert(sizeof(struct ng_render_state) == 96, "render state layout");
/* GLES3 core capabilities. Other enables keep their ordinary query path. */
static const uint32_t ng_state_caps[] = {0x0be2, 0x0c11, 0x0b71, 0x0b90,
                                         0x0b44};
static unsigned ng_state_cap_bit(uint32_t cap) {
  for (unsigned i = 0; i < sizeof(ng_state_caps) / sizeof(*ng_state_caps); i++)
    if (cap == ng_state_caps[i])
      return 1u << i;
  return 0;
}
#endif
