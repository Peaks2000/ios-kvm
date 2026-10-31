/* Noncryptographic change detection; read all bytes, including a partial last word. */
#ifndef IOS_KVM_SURFACE_FINGERPRINT_H
#define IOS_KVM_SURFACE_FINGERPRINT_H
#include <stdint.h>
#define XXH_INLINE_ALL
#define XXH_NAMESPACE ios_kvm_
#include "../../../xxhash/xxhash.h"
static inline uint64_t surface_fingerprint(const void *data, size_t bytes)
{
    return XXH64(data, bytes, 0);
}
#endif
