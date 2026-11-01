/* Actual guest IOSurface/GL async coherence check; use only a private candidate VM. */
#define GL_GLEXT_PROTOTYPES 1
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurfaceRef.h>
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <sys/sysctl.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
static void require(int ok,const char *what){if(!ok){fprintf(stderr,"async-check failed: %s\n",what);exit(1);}}
static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static int allow_missing_counter;
static uint64_t completed(void){uint64_t n=0;size_t size=sizeof(n);int ret=sysctlbyname("kern.ios_kvm_surface_completions",&n,&size,NULL,0);if(ret && errno==ENOENT && allow_missing_counter)return 0;require(!ret,"completion counter");return n;}
static IOSurfaceRef surface(int width,int height){
    CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const void *keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement};int vals[]={width,height,4};
    for(int i=0;i<3;i++){CFNumberRef n=CFNumberCreate(NULL,kCFNumberIntType,&vals[i]);CFDictionarySetValue(d,keys[i],n);CFRelease(n);}
    IOSurfaceRef s=IOSurfaceCreate(d);CFRelease(d);require(s!=NULL,"create surface");return s;
}
int main(int argc,char **argv){
    setvbuf(stdout,NULL,_IOLBF,0);
    int async_expected=argc<2 || strcmp(argv[1],"fallback");
    allow_missing_counter=!async_expected;
    void *(*create)(void *,uint32_t)=dlsym(RTLD_DEFAULT,"ios_kvm_readback_lease_create");
    void *(*set)(void *)=dlsym(RTLD_DEFAULT,"ios_kvm_readback_lease_set");
    void (*tag)(void *,uint32_t)=dlsym(RTLD_DEFAULT,"ios_kvm_readback_lease_surface");
    void (*destroy)(void *)=dlsym(RTLD_DEFAULT,"ios_kvm_readback_lease_destroy");
    require(create && set && tag && destroy,"lease functions");
    const int attrs[]={OSMESA_FORMAT,OSMESA_RGBA,OSMESA_DEPTH_BITS,0,OSMESA_STENCIL_BITS,0,OSMESA_PROFILE,OSMESA_ES_PROFILE,OSMESA_CONTEXT_MAJOR_VERSION,3,0};
    OSMesaContext ctx=OSMesaCreateContextAttribs(attrs,NULL);uint8_t window[16*16*4];
    require(ctx && OSMesaMakeCurrent(ctx,window,GL_UNSIGNED_BYTE,16,16),"Mesa context");
    printf("async-check renderer=%s version=%s expected_async=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),async_expected);
    uint64_t baseline=completed();unsigned frames=0;
    for(unsigned fixture=0;fixture<2;fixture++){
        int width=fixture?257:512,height=fixture?131:256;
        IOSurfaceRef s=surface(width,height);uint8_t *pixels=IOSurfaceGetBaseAddress(s);
        size_t stride=IOSurfaceGetBytesPerRow(s),bytes=stride*(height-1)+(size_t)width*4;
        require(pixels && stride%4==0 && bytes<=IOSurfaceGetAllocSize(s),"surface layout");
        memset(pixels,0xcd,IOSurfaceGetAllocSize(s));
        void *lease=create(pixels,bytes);require(lease!=NULL,"pin surface");
        GLuint texture,fbo;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"FBO");
        glPixelStorei(GL_PACK_ROW_LENGTH,stride/4);glPixelStorei(GL_PACK_ALIGNMENT,4);
        /* A caller-owned lease without an IOSurface tag must remain synchronous. */
        glClearColor(.125f,.25f,.5f,1);glClear(GL_COLOR_BUFFER_BIT);
        void *previous=set(lease);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);set(previous);
        size_t last=stride*(height-1)+(size_t)(width-1)*4;
        require(abs(pixels[0]-32)<=1 && abs(pixels[last]-32)<=1 && pixels[3]==255,"untagged lease stays synchronous");
        tag(lease,IOSurfaceGetID(s));
        for(unsigned frame=0;frame<20;frame++){
            require(!IOSurfaceLock(s,0,NULL),"producer lock");
            glDisable(GL_SCISSOR_TEST);glClearColor((frame+1)/32.f,.25f,.5f,1);glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);glScissor(7,5,width-14,height-10);glClearColor(.75f,(frame+1)/32.f,.125f,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
            uint64_t start=now();void *previous=set(lease);
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);set(previous);
            uint64_t submitted=now();require(!IOSurfaceUnlock(s,0,NULL),"producer unlock");
            require(!IOSurfaceLock(s,kIOSurfaceLockReadOnly,NULL),"consumer waits for pixels");uint64_t waited=now();
            unsigned errors=0;int red=((frame+1)*255+16)/32;
            for(int y=0;y<height;y++)for(int x=0;x<width;x++){
                int inner=x>=7 && x<width-7 && y>=5 && y<height-5;
                int expected[4]={inner?191:red,inner?red:64,inner?32:128,255};
                for(int c=0;c<4;c++)if(abs(pixels[y*stride+x*4+c]-expected[c])>1)errors++;
            }
            for(int y=0;y<height-1;y++)for(size_t x=(size_t)width*4;x<stride;x++)if(pixels[y*stride+x]!=0xcd)errors++;
            require(!IOSurfaceUnlock(s,kIOSurfaceLockReadOnly,NULL),"consumer unlock");
            GLenum gl=glGetError();printf("async-check fixture=%u frame=%u submit_us=%llu wait_us=%llu errors=%u gl=%#x\n",fixture,frame,submitted-start,waited-submitted,errors,gl);
            require(!errors && !gl,"fresh spatial pixels and padding");frames++;
        }
        /* A configured lease does not affect unscoped glReadPixels. */
        glPixelStorei(GL_PACK_ROW_LENGTH,0);uint8_t *ordinary=calloc((size_t)width*height,4);require(ordinary!=NULL,"ordinary buffer");
        glClearColor(.125f,.25f,.5f,1);glClear(GL_COLOR_BUFFER_BIT);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,ordinary);
        require(abs(ordinary[0]-32)<=1 && abs(ordinary[1]-64)<=1 && abs(ordinary[2]-128)<=1 && ordinary[3]==255,"ordinary read stays synchronous");free(ordinary);
        destroy(lease);CFRelease(s);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    }
    uint64_t deadline=now()+5000000,after=completed();
    while(async_expected && after<baseline+frames && now()<deadline){usleep(1000);after=completed();}
    require(after-baseline==(async_expected?frames:0),"negotiated async count");
    printf("async-check complete frames=%u kernel_completions=%llu\n",frames,after-baseline);
    OSMesaDestroyContext(ctx);return 0;
}
