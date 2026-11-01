#include <assert.h>
#include <stdio.h>
#include "gpu-shared-request.h"
#include "readback-wait-policy.h"

int main(void)
{
   /* An old server's busy reply must never authorize the extension. */
   const uint32_t old_replies[] = {0, 1, 2, UINT32_MAX, UINT32_C(0x494b0201)};
   const char *modes[] = {NULL, "auto", "0", "1", "", "invalid"};
   for (unsigned i=0;i<sizeof(old_replies)/sizeof(old_replies[0]);i++) {
      uint32_t features=ios_kvm_decode_features(old_replies[i]);
      assert(!features);
      for (unsigned j=0;j<sizeof(modes)/sizeof(modes[0]);j++)
         assert(!ios_kvm_upload_policy(features,modes[j]));
   }
   assert(!ios_kvm_decode_features(IOS_KVM_FEATURE_REPLY_BASE));
   assert(!ios_kvm_decode_features(IOS_KVM_FEATURE_REPLY_BASE|0x80));
   uint32_t supported=ios_kvm_decode_features(IOS_KVM_FEATURE_REPLY_BASE|IOS_KVM_FEATURE_UPLOAD|0x80);
   assert(supported==IOS_KVM_FEATURE_UPLOAD);
   assert(ios_kvm_upload_policy(supported,NULL));
   assert(ios_kvm_upload_policy(supported,"auto"));
   assert(ios_kvm_upload_policy(supported,"1"));
   assert(!ios_kvm_upload_policy(supported,"0"));
   assert(!ios_kvm_upload_policy(supported,""));
   assert(!ios_kvm_upload_policy(supported,"invalid"));
   assert(ios_kvm_decode_features(IOS_KVM_FEATURE_REPLY_BASE | IOS_KVM_FEATURE_KNOWN)==IOS_KVM_FEATURE_KNOWN);
   assert(!ios_kvm_skip_readback_wait(0,NULL,0));
   assert(!ios_kvm_skip_readback_wait(0,"auto",0));
   assert(ios_kvm_skip_readback_wait(1,NULL,0));
   assert(ios_kvm_skip_readback_wait(1,"auto",0));
   assert(!ios_kvm_skip_readback_wait(1,"0",0));
   assert(!ios_kvm_skip_readback_wait(1,"invalid",1));
   assert(!ios_kvm_skip_readback_wait(1,"marker",0));
   assert(ios_kvm_skip_readback_wait(0,"marker",1));
   assert(ios_kvm_skip_readback_wait(0,"1",0));
   puts("Upload policy: legacy busy replies cannot enable uploads; version/capability/mode checks passed");
}
