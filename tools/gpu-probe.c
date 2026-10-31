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

static GLuint shader(GLenum kind, const char *source)
{
    GLuint result=glCreateShader(kind);
    glShaderSource(result,1,&source,NULL); glCompileShader(result);
    GLint ok=0; glGetShaderiv(result,GL_COMPILE_STATUS,&ok);
    if (!ok) {
        char log[2048]; glGetShaderInfoLog(result,sizeof(log),NULL,log);
        fprintf(stderr,"GPU probe shader: %s\n",log);
        glDeleteShader(result); return 0;
    }
    return result;
}

/* Exercise texture sampling, an offscreen RGBA target, blending, orientation and
 * padded BGRA readback. A working clear alone does not establish these features. */
static int render_check(void)
{
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 330 core\nout vec2 uv;\n"
        "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "uv=p;gl_Position=vec4(p*2.-1.,0.,1.);}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 330 core\nin vec2 uv;out vec4 result;"
        "uniform sampler2D image;void main(){result=texture(image,uv);}");
    if (!vs || !fs) return 0;
    GLuint program=glCreateProgram();
    glAttachShader(program,vs); glAttachShader(program,fs); glLinkProgram(program);
    GLint ok=0; glGetProgramiv(program,GL_LINK_STATUS,&ok);
    glDeleteShader(vs); glDeleteShader(fs);
    if (!ok) {
        char log[2048]; glGetProgramInfoLog(program,sizeof(log),NULL,log);
        fprintf(stderr,"GPU probe program: %s\n",log);
        glDeleteProgram(program); return 0;
    }
    GLuint texture[2], fbo, vao;
    const unsigned char source[]={255,0,0,128, 0,255,0,128, 0,0,255,128, 255,255,255,128};
    glGenTextures(2,texture); glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,texture[0]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,source);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D,texture[1]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture[1],0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return 0;
    glViewport(0,0,8,8); glClearColor(.2,.4,.6,1); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program); glUniform1i(glGetUniformLocation(program,"image"),0);
    glBindTexture(GL_TEXTURE_2D,texture[0]);
    glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES,0,3);
    unsigned char pixels[8*10*4]; memset(pixels,0xa5,sizeof(pixels));
    glPixelStorei(GL_PACK_ROW_LENGTH,10);
    glReadPixels(0,0,8,8,GL_BGRA,GL_UNSIGNED_BYTE,pixels);
    int valid=glGetError()==GL_NO_ERROR;
    for (int y=0;y<8;y++) {
        for (int x=0;x<8;x++) {
            int quadrant=(y>=4)*2+(x>=4);
            for (int c=0;c<4;c++) {
                int rgba=c==0 ? 2 : c==2 ? 0 : c;
                const int background[]={51,102,153,255};
                int expected=rgba==3 ? 255 :
                    (source[quadrant*4+rgba]*128+background[rgba]*127+127)/255;
                if (abs(pixels[(y*10+x)*4+c]-expected)>2) valid=0;
            }
        }
        for (int i=8*4;i<10*4;i++) if (pixels[y*10*4+i]!=0xa5) valid=0;
    }
    glDeleteVertexArrays(1,&vao); glDeleteFramebuffers(1,&fbo);
    glDeleteTextures(2,texture); glDeleteProgram(program);
    return valid;
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
    if (!render_check()) return failed("Textured shader/blend/padded BGRA validation failed");
    fputs("{\"ok\":true,\"vendor\":",stdout); string((const char *)glGetString(GL_VENDOR));
    fputs(",\"renderer\":",stdout); string((const char *)glGetString(GL_RENDERER));
    fputs(",\"version\":",stdout); string((const char *)glGetString(GL_VERSION));
    fputs(",\"render_node\":",stdout); string(node);
    printf(",\"pixel\":[%u,%u,%u,%u],\"shader_pixels\":64,\"padded_bgra\":true}\n",pixel[0],pixel[1],pixel[2],pixel[3]);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroySurface(display,surface); eglDestroyContext(display,context); eglTerminate(display);
    return 0;
}
