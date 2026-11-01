/* Wire layout of the shared readback command; no guest/host pointer crosses it. */
#ifndef IOS_KVM_SHARED_REQUEST_H
#define IOS_KVM_SHARED_REQUEST_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define IOS_KVM_SHARED_UPLOAD_MIN_BYTES (256u << 10)

/* Private feature query through handle-zero RESOURCE_BUSY_WAIT. Legacy
 * servers ignore the flag and return only 0/1, never this versioned tag. */
#define IOS_KVM_FEATURE_QUERY_FLAGS UINT32_C(0x494b0000)
#define IOS_KVM_FEATURE_REPLY_BASE UINT32_C(0x494b0100)
#define IOS_KVM_FEATURE_UPLOAD UINT32_C(1)
#define IOS_KVM_FEATURE_SYNC_READBACK UINT32_C(2)
#define IOS_KVM_FEATURE_ASYNC_READBACK UINT32_C(4)
#define IOS_KVM_FEATURE_KNOWN (IOS_KVM_FEATURE_UPLOAD | IOS_KVM_FEATURE_SYNC_READBACK | IOS_KVM_FEATURE_ASYNC_READBACK)

static inline uint32_t ios_kvm_decode_features(uint32_t reply)
{
   return (reply & UINT32_C(0xffffff00)) == IOS_KVM_FEATURE_REPLY_BASE
      ? reply & IOS_KVM_FEATURE_KNOWN : 0;
}

static inline int ios_kvm_upload_policy(uint32_t features, const char *mode)
{
   if (!(features & IOS_KVM_FEATURE_UPLOAD)) return 0;
   return !mode || !strcmp(mode, "auto") || !strcmp(mode, "1");
}

struct ios_kvm_shared_request {
   uint32_t words[14]; /* vtest header followed by twelve command arguments */
   uint64_t segments[8192];
   uint32_t completion[3]; /* capacity for the trailer after the used segments */
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
/* Command66 has no socket reply. Pixels and status precede the release sequence
 * in a kernel-owned completion cell: uint32 status, uint32 sequence. */
static inline size_t
ios_kvm_prepare_async_request(struct ios_kvm_shared_request *request,
                              const uint32_t args[12], size_t segment_bytes,
                              uint64_t completion, uint32_t sequence)
{
   if (!completion || (completion & 7) || !sequence) return 0;
   size_t size = ios_kvm_prepare_shared_request(request, args, segment_bytes);
   if (!size) return 0;
   uint32_t trailer[3] = {(uint32_t)completion, (uint32_t)(completion >> 32), sequence};
   memcpy((char *)request + size, trailer, sizeof(trailer));
   request->words[0] += 3;
   request->words[1] = 66;
   return size + sizeof(trailer);
}
#endif
