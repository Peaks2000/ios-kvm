/* Read the display driver's versioned, coherent presentation snapshot. */
#include "../../vendor/hakios/xnu/files/iokit/Kernel/MiniIOMobileFramebufferState.h"
#include <stdio.h>
#include <sys/sysctl.h>

int main(void) {
  struct ios_kvm_display_state state = {0};
  size_t size = sizeof(state);
  if (sysctlbyname("kern.ios_kvm_display_state", &state, &size, NULL, 0)) {
    perror("kern.ios_kvm_display_state");
    return 1;
  }
  if (size != sizeof(state) || state.version != IOS_KVM_DISPLAY_STATE_VERSION ||
      state.size != sizeof(state)) {
    fputs("Unsupported display-state ABI\n", stderr);
    return 2;
  }
  printf("{\"version\":%u,\"sequence\":%llu,\"swap_id\":%u,"
         "\"layers_set\":%u,\"layers_cleared\":%u,\"width\":%u,\"height\":%u,"
         "\"panel_width\":%u,\"panel_height\":%u,\"row_bytes\":%u,\"layers\":[",
         state.version, (unsigned long long)state.sequence, state.swap_id,
         state.layers_set, state.layers_cleared, state.width, state.height,
         state.panel_width, state.panel_height, state.row_bytes);
  for (unsigned i = 0; i < 4; i++) {
    const struct ios_kvm_display_layer *layer = &state.layers[i];
    printf("%s{\"surface\":%u,\"enabled\":%u,\"source\":[%d,%d,%d,%d],"
           "\"destination\":[%d,%d,%d,%d]}", i ? "," : "", layer->surface,
           layer->enabled, layer->source[0], layer->source[1], layer->source[2],
           layer->source[3], layer->destination[0], layer->destination[1],
           layer->destination[2], layer->destination[3]);
  }
  puts("]}");
  return 0;
}
