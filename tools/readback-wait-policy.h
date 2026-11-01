#ifndef IOS_KVM_READBACK_WAIT_POLICY_H
#define IOS_KVM_READBACK_WAIT_POLICY_H
#include <string.h>

static inline int ios_kvm_skip_readback_wait(int synchronous, const char *mode, int marker)
{
   if (!mode || !strcmp(mode, "auto")) return !!synchronous;
   if (!strcmp(mode, "1")) return 1; /* Preserve the explicit legacy experiment. */
   if (!strcmp(mode, "marker")) return !!marker;
   return 0;
}
#endif
