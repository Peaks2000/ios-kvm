#include <assert.h>
#include <stdio.h>
#include "gpu-shared-request.h"

static struct {
   uint64_t before;
   struct ios_kvm_shared_request request;
   uint64_t after;
} checked;
static unsigned char original[sizeof(struct ios_kvm_shared_request)];

int main(void)
{
   checked.before = UINT64_C(0x1020304050607080);
   checked.after = UINT64_C(0xfedcba9876543210);
   for (unsigned i=0; i<8192; i++) checked.request.segments[i] = UINT64_C(0xf0000000) + i*16384;
   const unsigned counts[] = {1,2,3,16,64,305,512,1024,2048,4096};
   for (unsigned test=0; test<sizeof(counts)/sizeof(counts[0]); test++) {
      unsigned count=counts[test];
      uint32_t args[12] = {77,2,3008,0,1,2,0,750,1624,1,4988856,count};
      uint32_t header[2] = {12+count*4,64};
      memcpy(original,header,sizeof(header));
      memcpy(original+8,args,sizeof(args));
      memcpy(original+8+sizeof(args),checked.request.segments,count*16);
      size_t size=ios_kvm_prepare_shared_request(&checked.request,args,count*16);
      assert(size == 56+count*16);
      assert(!memcmp(original,&checked.request,size));
      uint32_t trailer[3] = {0x12345678, 1, 77};
      header[0] += 3; header[1] = 66;
      memcpy(original,header,sizeof(header));
      memcpy(original+size,trailer,sizeof(trailer));
      size_t async_size=ios_kvm_prepare_async_request(&checked.request,args,count*16,UINT64_C(0x112345678),77);
      assert(async_size==size+12 && !memcmp(original,&checked.request,async_size));
      assert(checked.before == UINT64_C(0x1020304050607080));
      assert(checked.after == UINT64_C(0xfedcba9876543210));
   }
   uint32_t args[12] = {0};
   assert(!ios_kvm_prepare_shared_request(&checked.request,args,0));
   assert(!ios_kvm_prepare_shared_request(&checked.request,args,16));
   args[11]=1;
   assert(!ios_kvm_prepare_shared_request(&checked.request,args,17));
   args[11]=4097;
   assert(!ios_kvm_prepare_shared_request(&checked.request,args,4097*16));
   args[11]=1;
   assert(!ios_kvm_prepare_async_request(&checked.request,args,16,0,1));
   assert(!ios_kvm_prepare_async_request(&checked.request,args,16,0x1001,1));
   assert(!ios_kvm_prepare_async_request(&checked.request,args,16,0x1000,0));
   puts("shared request: old/new wire bytes identical, bounds/count checks and guards passed");
   return 0;
}
