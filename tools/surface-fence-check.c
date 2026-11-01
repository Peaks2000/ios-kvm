/* Opt-in kernel completion/lifetime check. The host harness supplies completions. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurfaceRef.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysctl.h>

static void require(int ok, const char *what)
{
    if (!ok) { fprintf(stderr,"surface-fence-check: failed: %s\n",what); exit(1); }
}
static io_connect_t pin(void *address, size_t bytes, uint64_t *first)
{
    io_service_t service=IOServiceGetMatchingService(MACH_PORT_NULL,IOServiceMatching("IOCoreSurfaceRoot"));
    require(service!=0,"root service"); io_connect_t connection=0;
    kern_return_t ret=IOServiceOpen(service,mach_task_self(),0,&connection); IOObjectRelease(service);
    require(ret==KERN_SUCCESS,"open root service");
    uint64_t input[]={ (uintptr_t)address,bytes },segments[8192]; size_t size=sizeof(segments);
    ret=IOConnectCallMethod(connection,0x100,input,2,NULL,0,NULL,NULL,segments,&size);
    require(ret==KERN_SUCCESS && size>=16 && size%16==0,"prepare owning-task pin");
    *first=segments[0]; return connection;
}
static kern_return_t prepare(io_connect_t connection, uint64_t sid, size_t bytes, uint64_t output[2])
{
    uint64_t input[]={sid,bytes}; uint32_t count=2;
    return IOConnectCallScalarMethod(connection,0x102,input,2,output,&count);
}
static IOSurfaceRef make_surface(int width,int height)
{
    CFMutableDictionaryRef properties=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const void *keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement};
    int values[]={width,height,4};
    for(unsigned i=0;i<3;i++){CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,&values[i]);CFDictionarySetValue(properties,keys[i],n);CFRelease(n);}
    IOSurfaceRef surface=IOSurfaceCreate(properties);CFRelease(properties);require(surface!=NULL,"create kernel-backed surface");
    return surface;
}
static void quota_check(int byte_quota)
{
    unsigned limit=byte_quota?2:128;
    size_t bytes=byte_quota?(32u<<20):16384;
    IOSurfaceRef surfaces[129]={0};
    uint64_t physical[128],completion[128],sequence[128];
    for(unsigned i=0;i<=limit;i++) {
        surfaces[i]=make_surface(byte_quota && i<limit?4096:64,byte_quota && i<limit?2048:64);
        size_t span=i<limit?bytes:16384;
        uint64_t first=0,out[2];
        io_connect_t connection=pin(IOSurfaceGetBaseAddress(surfaces[i]),span,&first);
        kern_return_t ret=prepare(connection,IOSurfaceGetID(surfaces[i]),span,out);
        require(i==limit ? ret==kIOReturnNoResources : ret==KERN_SUCCESS,"bounded pending surface count");
        IOServiceClose(connection);
        if(i<limit){physical[i]=first;completion[i]=out[0];sequence[i]=out[1];}
    }
    printf("{\"event\":\"prepared\",\"mode\":\"%s\",\"bytes\":%zu,\"fences\":[",byte_quota?"byte-quota":"quota",limit*bytes);
    for(unsigned i=0;i<limit;i++)printf("%s{\"physical\":%llu,\"completion\":%llu,\"sequence\":%llu}",i?",":"",physical[i],completion[i],sequence[i]);
    puts("]}");
    /* All submitting task references disappear before the host publishes. */
    _exit(0);
}
int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IOLBF,0);
    const char *mode=argc>1?argv[1]:"normal";
    if (!strcmp(mode,"state")) {
        const char *names[]={"kern.ios_kvm_pending_surfaces","kern.ios_kvm_pending_surface_bytes","kern.ios_kvm_surface_completions"};
        uint64_t counters[3]={0};
        for(unsigned i=0;i<3;i++){size_t length=sizeof(counters[i]);require(!sysctlbyname(names[i],&counters[i],&length,NULL,0),"completion counters");}
        char bootargs[4096]={0};size_t length=sizeof(bootargs)-1;
        require(!sysctlbyname("kern.bootargs",bootargs,&length,NULL,0),"boot arguments");
        printf("{\"enabled\":%s,\"counts\":[%llu,%llu,%llu]}\n",strstr(bootargs,"gpu_async=1")?"true":"false",counters[0],counters[1],counters[2]);
        return 0;
    }
    if(!strcmp(mode,"quota") || !strcmp(mode,"byte-quota")) quota_check(!strcmp(mode,"byte-quota"));
    IOSurfaceRef surface=make_surface(64,64);
    uint32_t sid=IOSurfaceGetID(surface);void *base=IOSurfaceGetBaseAddress(surface);
    size_t bytes=IOSurfaceGetBytesPerRow(surface)*(IOSurfaceGetHeight(surface)-1)+IOSurfaceGetWidth(surface)*4;
    require(base && bytes<=IOSurfaceGetAllocSize(surface),"surface backing");
    uint64_t physical=0,output[2]={0};
    void *other=NULL;require(!posix_memalign(&other,16384,bytes),"mismatch allocation");
    io_connect_t wrong=pin(other,bytes,&physical);
    require(prepare(wrong,sid,bytes,output)!=KERN_SUCCESS,"reject unrelated pinned memory");
    IOServiceClose(wrong);free(other);
    io_connect_t connection=pin(base,bytes,&physical);
    if(!strcmp(mode,"disabled")) {
        require(prepare(connection,sid,bytes,output)==kIOReturnUnsupported,"default disabled API");
        IOServiceClose(connection);CFRelease(surface);puts("completion API disabled as expected");return 0;
    }
    require(prepare(connection,0,bytes,output)!=KERN_SUCCESS,"reject invalid surface ID");
    require(prepare(connection,sid,bytes+1,output)!=KERN_SUCCESS,"reject pin length mismatch");
    require(prepare(connection,sid,bytes,output)==KERN_SUCCESS,"prepare completion");
    uint64_t repeat[2];require(prepare(connection,sid,bytes,repeat)==kIOReturnBusy,"one pending completion per surface");
    uint64_t start=mach_absolute_time();
    kern_return_t ret=IOSurfaceLock(surface,kIOSurfaceLockReadOnly|kIOSurfaceLockAvoidSync,NULL);
    require(ret==kIOReturnCannotLock,"avoidSync refuses incomplete pixels");
    printf("{\"event\":\"prepared\",\"sid\":%u,\"physical\":%llu,\"bytes\":%zu,\"completion\":%llu,\"sequence\":%llu,\"pid\":%d,\"mode\":\"%s\"}\n",sid,physical,bytes,output[0],output[1],getpid(),mode);
    if(!strcmp(mode,"normal")) { IOServiceClose(connection);connection=0; }
    if(!strcmp(mode,"exit")) _exit(0); /* Kernel closes both user clients while completion is pending. */
    if(strcmp(mode,"timeout")) { char begin;require(read(STDIN_FILENO,&begin,1)==1,"host ready handshake");puts("{\"event\":\"waiting\"}"); }
    start=mach_absolute_time();ret=IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL);
    mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
    uint64_t elapsed=(mach_absolute_time()-start)*timebase.numer/timebase.denom/1000;
    printf("{\"event\":\"finished\",\"result\":%u,\"wait_us\":%llu}\n",ret,elapsed);
    if(!strcmp(mode,"timeout"))require(ret==kIOReturnTimeout,"bounded CPU lock wait");
    else if(!strcmp(mode,"error"))require(ret==kIOReturnError,"host error reaches CPU lock");
    else {
        require(ret==KERN_SUCCESS,"completed CPU lock");
        require(*(volatile uint32_t *)base==0x12345678,"fresh pixels visible after completion");
        IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
    }
    if(connection) IOServiceClose(connection);
    CFRelease(surface);return 0;
}
