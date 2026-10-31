/* Measure repeated complete GPU readbacks, validating freshly rendered pixels each time. */
#define GL_GLEXT_PROTOTYPES 1
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t microseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static int run(int width, int height, int frames)
{
    if (width < 1 || height < 1 || width > 4096 || height > 4096) return 1;
    const int attributes[] = {OSMESA_FORMAT, OSMESA_RGBA, OSMESA_DEPTH_BITS, 0,
        OSMESA_STENCIL_BITS, 0, OSMESA_PROFILE, OSMESA_ES_PROFILE,
        OSMESA_CONTEXT_MAJOR_VERSION, 3, OSMESA_CONTEXT_MINOR_VERSION, 0, 0};
    OSMesaContext context = OSMesaCreateContextAttribs(attributes, NULL);
    uint8_t window[16 * 16 * 4];
    if (!context || !OSMesaMakeCurrent(context, window, GL_UNSIGNED_BYTE, 16, 16)) return 1;
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("gpu-bench: %s; %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    GLuint texture, framebuffer;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 1;
    glViewport(0, 0, width, height);
    size_t bytes = (size_t)width * height * 4;
    uint8_t *pixels = malloc(bytes);
    if (!pixels) return 1;
    unsigned errors = 0;
    for (int frame = 0; frame < frames; frame++) {
        uint64_t start = microseconds();
        glClearColor((frame + 1) / 32.0f, 0.25f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        uint64_t draw = microseconds();
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        uint64_t read = microseconds();
        int expected[] = {((frame + 1) * 255 + 16) / 32, 64, 128, 255};
        size_t indices[] = {0, ((size_t)width * height / 2) * 4, bytes - 4};
        for (int p = 0; p < 3; p++)
            for (int c = 0; c < 4; c++)
                if (abs((int)pixels[indices[p] + c] - expected[c]) > 1) errors++;
        GLenum error = glGetError();
        if (error) errors++;
        printf("gpu-bench: frame=%d draw_us=%llu read_us=%llu errors=%u gl=%#x\n",
            frame, draw-start, read-draw, errors, error);
    }
    free(pixels);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    OSMesaDestroyContext(context);
    return errors ? 1 : 0;
}

static int flip_benchmark(void)
{
    const int attrs[] = {OSMESA_FORMAT, OSMESA_RGBA, OSMESA_DEPTH_BITS, 0, OSMESA_STENCIL_BITS, 0,
        OSMESA_PROFILE, OSMESA_ES_PROFILE, OSMESA_CONTEXT_MAJOR_VERSION, 3, 0};
    OSMesaContext context = OSMesaCreateContextAttribs(attrs, NULL);
    uint8_t window[16*16*4];
    if (!context || !OSMesaMakeCurrent(context, window, GL_UNSIGNED_BYTE, 16, 16)) return 1;
    const int width=750, height=1624, stride=(width+8)*4;
    uint8_t *pixels=malloc((size_t)stride*height), *temporary=malloc((size_t)stride*height);
    if (!pixels || !temporary) return 1;
    GLuint textures[2], fbos[2];
    glGenTextures(2,textures); glGenFramebuffers(2,fbos);
    for (int i=0;i<2;i++) {
        glBindTexture(GL_TEXTURE_2D,textures[i]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glBindFramebuffer(GL_FRAMEBUFFER,fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i],0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return 1;
    }
    printf("flip-bench: %s\n",glGetString(GL_RENDERER));
    glPixelStorei(GL_PACK_ROW_LENGTH,width+8);
    unsigned errors=0;
    for (int frame=0;frame<20;frame++) {
        int gpu=frame%2;
        float red=(frame+1)/32.f;
        glBindFramebuffer(GL_FRAMEBUFFER,fbos[0]);
        glDisable(GL_SCISSOR_TEST); glClearColor(red,.25,.5,1); glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST); glScissor(0,height/2,width,height/2);
        glClearColor(red,.75,.5,1); glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
        glFinish();
        uint64_t start=microseconds();
        if (gpu) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbos[1]);
            glBlitFramebuffer(0,0,width,height,0,height,width,0,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[1]);
            glReadPixels(0,0,width,height,GL_BGRA,GL_UNSIGNED_BYTE,pixels);
        } else {
            glReadPixels(0,0,width,height,GL_BGRA,GL_UNSIGNED_BYTE,temporary);
            for(int y=0;y<height;y++) memcpy(pixels+(size_t)(height-1-y)*stride,
                temporary+(size_t)y*stride,width*4);
        }
        uint64_t elapsed=microseconds()-start;
        int xs[]={0,width/2,width-1}, ys[]={0,height/4,height-1};
        for(int yi=0;yi<3;yi++) for(int xi=0;xi<3;xi++) {
            uint8_t *p=pixels+(size_t)ys[yi]*stride+xs[xi]*4;
            int expected[]={128,ys[yi]<height/2?191:64,(int)(red*255+.5),255};
            for(int c=0;c<4;c++) if(abs(p[c]-expected[c])>1) errors++;
        }
        GLenum error=glGetError(); if(error) errors++;
        printf("flip-bench: frame=%d gpu=%d us=%llu errors=%u gl=%#x\n",frame,gpu,elapsed,errors,error);
    }
    free(pixels); free(temporary); OSMesaDestroyContext(context);
    return errors?1:0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "flip")) return flip_benchmark();
    if (argc > 1 && !strcmp(argv[1], "contexts")) {
        for (unsigned i = 0; i < 10; i++)
            if (run(64, 64, 1)) return 1;
        puts("gpu-bench: 10 context create/render/destroy cycles passed");
        return 0;
    }
    return run(argc > 2 ? atoi(argv[1]) : 750, argc > 2 ? atoi(argv[2]) : 1624, 20);
}
