/* Read the display driver's versioned, coherent presentation snapshot. */
#include "../../vendor/hakios/xnu/files/iokit/Kernel/MiniIOMobileFramebufferState.h"
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

int main(int argc, char **argv) {
  int keys = argc == 2 && !strcmp(argv[1], "--frame");
  if (argc != 1 && !keys) {
    fputs("usage: display-state [--frame]\n", stderr);
    return 2;
  }
  struct ios_kvm_display_frame frame = {0};
  struct ios_kvm_display_state state;
  size_t size = keys ? sizeof(frame) : sizeof(frame.presentation);
  const char *name = keys ? "kern.ios_kvm_display_frame" : "kern.ios_kvm_display_state";
  void *output = keys ? (void *)&frame : (void *)&frame.presentation;
  if (sysctlbyname(name, output, &size, NULL, 0)) {
    perror(name);
    return 1;
  }
  state = frame.presentation;
  if (size != (keys ? sizeof(frame) : sizeof(state)) ||
      (keys && (frame.version != IOS_KVM_DISPLAY_FRAME_VERSION || frame.size != sizeof(frame))) ||
      state.version != IOS_KVM_DISPLAY_STATE_VERSION ||
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
  putchar(']');
  if (keys) {
    printf(",\"frame_version\":%u,\"frame_size\":%u,\"keys\":[", frame.version, frame.size);
    for (unsigned i = 0; i < 4; i++) {
      const struct ios_kvm_surface_frame_key *key = frame.keys + i;
      printf("%s{\"epoch\":%llu,\"cell\":%llu,\"sequence\":%u}", i ? "," : "",
             (unsigned long long)key->epoch, (unsigned long long)key->cell, key->sequence);
    }
    putchar(']');
  }
  puts("}");
  return 0;
}
