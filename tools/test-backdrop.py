#!/usr/bin/env python3
"""Test actual bridge shaders/snapshot functions; --host runs offscreen without a VM."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', action='store_true', help='Build and run with native EGL/GLES, without a VM')
parser.add_argument('--mode', choices=('auto', 'hardware', 'software'), default='auto')
parser.add_argument('--native-fetch', action='store_true', help='Allow native framebuffer fetch instead of forcing the snapshot branch')
parser.add_argument('--benchmark', action='store_true', help='With --host, compare full and partial snapshot copies')
args = parser.parse_args()
if args.benchmark and (not args.host or args.native_fetch):
    parser.error('--benchmark requires --host and the default snapshot branch')
source = (root / 'vendor/hakios/userland/src/eagl.c').read_text()
macro = source[source.index('#define BACKDROP_SHADER'):source.index('static const char vibrant_fragment')]
if not args.native_fetch:
    macro = macro.replace('"#ifdef GL_EXT_shader_framebuffer_fetch\\n"', '"#if 0\\n"')
pieces = [macro]
for name in ('vibrant_fragment', 'group_over_fragment', 'group_flat_fragment'):
    match = re.search(r'static const char ' + name + r'\[\] =.*?;\n', source, re.S)
    pieces.append(match.group())
pieces.append(source[source.index('static void\nbackdrop_begin'):source.index('enum { CENSUS_NOT_DRAWN')])
out = root / '.local/support/backdrop-test'
if args.host:
    out = root / '.local/backdrop-test-host'
out.parent.mkdir(parents=True, exist_ok=True)
generated = out.with_suffix('.c')
generated.write_text(r'''
#define GL_GLEXT_PROTOTYPES 1
#ifdef IOS_KVM_HOST_TEST
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#else
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now_us(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec*1e6+t.tv_nsec/1e3;
}
static int compare_time(const void *a,const void *b) {
    double x=*(const double *)a,y=*(const double *)b; return (x>y)-(x<y);
}
static unsigned copied_pixels;
static void copy_snapshot(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                          GLint x, GLint y, GLsizei width, GLsizei height) {
    copied_pixels += width*height;
    glCopyTexSubImage2D(target,level,xoffset,yoffset,x,y,width,height);
}
#define glCopyTexSubImage2D copy_snapshot
struct eagl {
    GLuint behind_texture, behind_programs[3];
    GLint behind_width, behind_height, behind_binding, behind_locations[3];
    bool behind_bound;
};
''' + '\n'.join(pieces) + r'''
static GLuint shader(GLenum type, const char *text) {
    GLuint result = glCreateShader(type); GLint ok;
    glShaderSource(result, 1, &text, NULL); glCompileShader(result);
    glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(result, sizeof(log), NULL, log); puts(log); exit(1); }
    return result;
}
int main(void) {
#ifdef IOS_KVM_HOST_TEST
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform=(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!platform) return 1;
    EGLDisplay display=platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);
    EGLint major, minor, count;
    if (!eglInitialize(display,&major,&minor) || !eglBindAPI(EGL_OPENGL_ES_API)) return 1;
    const EGLint config_attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;
    if (!eglChooseConfig(display,config_attrs,&config,1,&count) || !count) return 1;
    const EGLint context_attrs[]={EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,1,EGL_NONE};
    EGLContext ctx=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);
    const EGLint surface_attrs[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,surface_attrs);
    if (ctx==EGL_NO_CONTEXT || surface==EGL_NO_SURFACE || !eglMakeCurrent(display,surface,surface,ctx)) return 1;
#else
    const int attrs[] = { OSMESA_FORMAT, OSMESA_RGBA, OSMESA_PROFILE, OSMESA_ES_PROFILE,
        OSMESA_CONTEXT_MAJOR_VERSION, 3, OSMESA_DEPTH_BITS, 0, OSMESA_STENCIL_BITS, 0, 0 };
    unsigned char window[16*16*4];
    OSMesaContext ctx = OSMesaCreateContextAttribs(attrs, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, window, GL_UNSIGNED_BYTE, 16, 16)) return 1;
#endif
    printf("backdrop-test: %s\n", glGetString(GL_RENDERER));
    const char *vertex = "#version 300 es\n out lowp vec4 color; out highp vec2 texcoord;"
        "void main() { vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "gl_Position=vec4(p*2.-1.,0.,1.); color=vec4(1.); texcoord=vec2(.5); }";
    const char *fragments[] = {vibrant_fragment, group_over_fragment, group_flat_fragment};
    GLuint target, mask, fbo, sentinel;
    glGenTextures(1, &target); glBindTexture(GL_TEXTURE_2D,target);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 1;
    glGenTextures(1,&mask); glBindTexture(GL_TEXTURE_2D,mask);
    unsigned char rgba[] = {255,255,255,64};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glGenTextures(1,&sentinel); glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D,sentinel);
    glActiveTexture(GL_TEXTURE0); glViewport(0,0,8,8);
    struct eagl e = {0};
    GLuint programs[3]; /* Match production: cached internal programs live for the context. */
    unsigned errors = 0;
    /* Disabled, partial, clipped edges, empty, and outside the target. */
    const int boxes[][5] = {{0,0,0,0,0}, {1,2,1,3,4}, {1,-2,-1,5,4}, {1,6,5,8,9},
        {1,2,2,0,4}, {1,2,2,4,0}, {1,8,8,2,2}, {1,-9,-9,2,2}};
    for (int test = 0; test < 3; test++) {
        GLuint program = glCreateProgram(); GLint ok;
        programs[test]=program;
        glAttachShader(program,shader(GL_VERTEX_SHADER,vertex));
        glAttachShader(program,shader(GL_FRAGMENT_SHADER,fragments[test]));
        glLinkProgram(program); glGetProgramiv(program,GL_LINK_STATUS,&ok);
        if (!ok) return 1;
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program,"texture0"),0);
        glUniform4f(glGetUniformLocation(program,"texmat"),1,1,0,0);
        const float matrix[] = {-1,0,0,0, 0,-1,0,0, 0,0,-1,0, 0,0,0,1, 1,1,1,0};
        glUniform4fv(glGetUniformLocation(program,"colours"),5,matrix);
        glUniform4f(glGetUniformLocation(program,"flat_colour"),.1,.2,.3,.4);
        for (unsigned box=0;box<sizeof(boxes)/sizeof(boxes[0]);box++) for (int frame=0;frame<3;frame++) {
            float red=(frame+1)*.1f;
            float alpha = test == 2 ? (frame+1)*.25f : 1;
            glDisable(GL_SCISSOR_TEST);
            glClearColor(red,.4,.6,alpha); glClear(GL_COLOR_BUFFER_BIT);
            const int *clip=boxes[box];
            glScissor(clip[1],clip[2],clip[3],clip[4]);
            if (clip[0]) glEnable(GL_SCISSOR_TEST);
            /* Read and draw FBOs deliberately differ: the helper must copy the draw target. */
            glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
            unsigned before_copy=copied_pixels;
            backdrop_begin(&e,program);
            GLint binding; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&binding);
            if (binding != 0) errors++;
            glDrawArrays(GL_TRIANGLES,0,3); backdrop_end(&e);
            glActiveTexture(GL_TEXTURE7); glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
            if (binding != (GLint)sentinel) errors++;
            glActiveTexture(GL_TEXTURE0); glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);
            unsigned char pixels[8*8*4]; glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            float expected[] = {red+(1-2*red)*(64./255), .4+.2*(64./255), .6-.2*(64./255), 1};
            if (test==2) { expected[0]=.1*alpha; expected[1]=.2*alpha; expected[2]=.3*alpha; expected[3]=.4*alpha; }
            unsigned covered=0;
            for (int y=0;y<8;y++) for (int x=0;x<8;x++) {
                bool inside=!clip[0] || ((int64_t)x>=clip[1] && (int64_t)x<(int64_t)clip[1]+clip[3] &&
                    (int64_t)y>=clip[2] && (int64_t)y<(int64_t)clip[2]+clip[4]);
                float background[]={red,.4,.6,alpha};
                covered+=inside;
                for(int c=0;c<4;c++)
                    if (abs(pixels[(y*8+x)*4+c]-(int)((inside?expected[c]:background[c])*255+.5))>2) errors++;
            }
            unsigned wanted_copy=glGetUniformLocation(program,"hakios_behind")>=0 ? covered : 0;
            if (copied_pixels-before_copy!=wanted_copy) errors++;
            GLenum error=glGetError(); if (error) errors++;
            printf("backdrop-test: shader=%d box=%u frame=%d copied_pixels=%u errors=%u gl=%#x\n",
                test,box,frame,copied_pixels-before_copy,errors,error);
        }
    }
#ifdef IOS_KVM_BACKDROP_BENCH
    glDisable(GL_SCISSOR_TEST); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glBindTexture(GL_TEXTURE_2D,target);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,750,1624,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glClearColor(.2,.4,.6,1); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(programs[0]); glScissor(100,100,120,80);
    for (int phase=0;phase<4;phase++) {
        bool partial=phase==1 || phase==2; /* ABBA, same production helper. */
        (partial?glEnable:glDisable)(GL_SCISSOR_TEST);
        double samples[16];
        for (int run=0;run<20;run++) {
            glFinish(); double began=now_us();
            for (int repeat=0;repeat<32;repeat++) { backdrop_begin(&e,programs[0]); backdrop_end(&e); }
            glFinish(); double elapsed=(now_us()-began)/32;
            if (run>=4) samples[run-4]=elapsed;
        }
        qsort(samples,16,sizeof(*samples),compare_time);
        GLenum error=glGetError(); if (error) errors++;
        printf("backdrop-copy: phase=%d partial=%d copied_pixels=%d median_us=%.2f gl=%#x\n",
            phase,partial,partial?120*80:750*1624,(samples[7]+samples[8])/2,error);
    }
#endif
    for (int i=0;i<3;i++) glDeleteProgram(programs[i]);
#ifdef IOS_KVM_HOST_TEST
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroySurface(display,surface); eglDestroyContext(display,ctx); eglTerminate(display);
#else
    OSMesaDestroyContext(ctx);
#endif
    return errors ? 1 : 0;
}
''')
if args.host:
    from gpu_host import select_gpu
    profile=select_gpu(args.mode)['selected']
    if not profile:
        raise SystemExit('No validated host renderer')
    flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','egl','glesv2'],text=True))
    options=['-DIOS_KVM_BACKDROP_BENCH'] if args.benchmark else []
    subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-DIOS_KVM_HOST_TEST',
        *options,str(generated),'-o',str(out),*flags],check=True)
    subprocess.run([str(out)],env={**os.environ,**profile['environment']},check=True)
else:
    subprocess.run(['clang', '-target', 'arm64-apple-ios16.0', '-isysroot', str(root / '.local/sdks/iPhoneOS16.5.sdk'),
                '-fuse-ld=lld', '-O2', '-I', str(root / '.local/mesa-25.0.7/include'),
                '-Wl,-rpath,@executable_path', '-Wl,-rpath,/redwood/share/gpu-bundle', str(generated),
                str(root / '.local/support/libOSMesa.8.dylib'), '-o', str(out)], check=True)
print(out)
