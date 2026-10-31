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

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "contexts")) {
        for (unsigned i = 0; i < 10; i++)
            if (run(64, 64, 1)) return 1;
        puts("gpu-bench: 10 context create/render/destroy cycles passed");
        return 0;
    }
    return run(argc > 2 ? atoi(argv[1]) : 750, argc > 2 ? atoi(argv[2]) : 1624, 20);
}
