/* Probe the actual EGL/OpenGL renderer and a pixel, rather than inferring acceleration from PCI IDs. */
#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void string(const char *value)
{
    if (!value) { fputs("null", stdout); return; }
    putchar('"');
    for (const unsigned char *p=(const unsigned char *)value; *p; p++) {
        if (*p=='"' || *p=='\\') printf("\\%c",*p);
        else if (*p<32) printf("\\u%04x",*p);
        else putchar(*p);
    }
    putchar('"');
}

static int failed(const char *message)
{
    fputs("{\"ok\":false,\"error\":",stdout); string(message);
    printf(",\"egl_error\":%u}\n",eglGetError()); return 1;
}

int main(int argc, char **argv)
{
    PFNEGLQUERYDEVICESEXTPROC query=(void *)eglGetProcAddress("eglQueryDevicesEXT");
    PFNEGLQUERYDEVICESTRINGEXTPROC device_string=(void *)eglGetProcAddress("eglQueryDeviceStringEXT");
    PFNEGLGETPLATFORMDISPLAYEXTPROC display_for=(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDeviceEXT devices[32]; EGLint count=0;
    if (query && !query(32,devices,&count)) count=0;
    if (count>32) count=32;
    if (argc==2 && !strcmp(argv[1],"--list")) {
        putchar('[');
        for (int i=0;i<count;i++) {
            if (i) putchar(',');
            printf("{\"index\":%d,\"render_node\":",i);
            string(device_string ? device_string(devices[i],EGL_DRM_RENDER_NODE_FILE_EXT) : NULL);
            putchar('}');
        }
        puts("]"); return 0;
    }
    if (!display_for) return failed("EGL platform displays unavailable");
    const char *node=NULL;
    EGLDisplay display;
    if (argc==3 && !strcmp(argv[1],"--device")) {
        char *end; long index=strtol(argv[2],&end,10);
        if (*end || index<0 || index>=count) return failed("Invalid EGL device index");
        if (device_string) node=device_string(devices[index],EGL_DRM_RENDER_NODE_FILE_EXT);
        display=display_for(EGL_PLATFORM_DEVICE_EXT,devices[index],NULL);
    } else if (argc==2 && !strcmp(argv[1],"--default")) {
        display=display_for(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);
    } else return failed("usage: gpu-probe --list | --device INDEX | --default");
    EGLint major,minor;
    if (display==EGL_NO_DISPLAY || !eglInitialize(display,&major,&minor)) return failed("Cannot initialize EGL display");
    if (!eglBindAPI(EGL_OPENGL_API)) return failed("Desktop OpenGL unavailable");
    const EGLint config_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config; EGLint configs;
    if (!eglChooseConfig(display,config_attrs,&config,1,&configs) || !configs) return failed("No RGBA OpenGL configuration");
    EGLContext context=EGL_NO_CONTEXT;
    const int versions[][2]={{4,3},{3,3}};
    for (int i=0;i<2 && context==EGL_NO_CONTEXT;i++) {
        const EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION_KHR,versions[i][0],EGL_CONTEXT_MINOR_VERSION_KHR,versions[i][1],
            EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,EGL_NONE};
        context=eglCreateContext(display,config,EGL_NO_CONTEXT,attrs);
    }
    if (context==EGL_NO_CONTEXT) return failed("OpenGL 3.3 or newer context unavailable");
    const EGLint surface_attrs[]={EGL_WIDTH,8,EGL_HEIGHT,8,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,surface_attrs);
    if (surface==EGL_NO_SURFACE || !eglMakeCurrent(display,surface,surface,context)) return failed("Cannot activate OpenGL context");
    unsigned char pixel[4]={0};
    glClearColor(.25,.5,.75,1); glClear(GL_COLOR_BUFFER_BIT); glFinish();
    glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    const int expected[]={64,128,191,255};
    for (int i=0;i<4;i++) if (abs(pixel[i]-expected[i])>1) return failed("OpenGL pixel validation failed");
    if (glGetError()!=GL_NO_ERROR) return failed("OpenGL error during pixel validation");
    fputs("{\"ok\":true,\"vendor\":",stdout); string((const char *)glGetString(GL_VENDOR));
    fputs(",\"renderer\":",stdout); string((const char *)glGetString(GL_RENDERER));
    fputs(",\"version\":",stdout); string((const char *)glGetString(GL_VERSION));
    fputs(",\"render_node\":",stdout); string(node);
    printf(",\"pixel\":[%u,%u,%u,%u]}\n",pixel[0],pixel[1],pixel[2],pixel[3]);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroySurface(display,surface); eglDestroyContext(display,context); eglTerminate(display);
    return 0;
}
