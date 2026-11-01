/* Wire layout of the shared readback command; no guest/host pointer crosses it. */
#ifndef IOS_KVM_SHARED_REQUEST_H
#define IOS_KVM_SHARED_REQUEST_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define IOS_KVM_SHARED_UPLOAD_MIN_BYTES (256u << 10)

struct ios_kvm_shared_request {
   uint32_t words[14]; /* vtest header followed by twelve command arguments */
   uint64_t segments[8192];
};
_Static_assert(offsetof(struct ios_kvm_shared_request, segments) == 14 * sizeof(uint32_t),
               "shared request must have no wire padding");

static inline size_t
ios_kvm_prepare_shared_request(struct ios_kvm_shared_request *request,
                               const uint32_t args[12], size_t segment_bytes)
{
   if (!segment_bytes || segment_bytes > sizeof(request->segments) ||
       segment_bytes % 16 || args[11] != segment_bytes / 16)
      return 0;
   request->words[0] = 12 + args[11] * 4;
   request->words[1] = 64;
   memcpy(request->words + 2, args, 12 * sizeof(uint32_t));
   return sizeof(request->words) + segment_bytes;
}
#endif
