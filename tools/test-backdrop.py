#!/usr/bin/env python3
"""Build a guest pixel test from the actual bridge shaders and snapshot functions."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
source = (root / 'vendor/hakios/userland/src/eagl.c').read_text()
macro = source[source.index('#define BACKDROP_SHADER'):source.index('static const char vibrant_fragment')]
pieces = [macro]
for name in ('vibrant_fragment', 'group_over_fragment', 'group_flat_fragment'):
    match = re.search(r'static const char ' + name + r'\[\] =.*?;\n', source, re.S)
    pieces.append(match.group())
pieces.append(source[source.index('static void\nbackdrop_begin'):source.index('enum { CENSUS_NOT_DRAWN')])
out = root / '.local/support/backdrop-test'
generated = out.with_suffix('.c')
generated.write_text(r'''
#define GL_GLEXT_PROTOTYPES 1
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
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
    const int attrs[] = { OSMESA_FORMAT, OSMESA_RGBA, OSMESA_PROFILE, OSMESA_ES_PROFILE,
        OSMESA_CONTEXT_MAJOR_VERSION, 3, OSMESA_DEPTH_BITS, 0, OSMESA_STENCIL_BITS, 0, 0 };
    unsigned char window[16*16*4];
    OSMesaContext ctx = OSMesaCreateContextAttribs(attrs, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, window, GL_UNSIGNED_BYTE, 16, 16)) return 1;
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
    unsigned errors = 0;
    for (int test = 0; test < 3; test++) {
        GLuint program = glCreateProgram(); GLint ok;
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
        for (int frame=0;frame<3;frame++) {
            float red=(frame+1)*.1f;
            float alpha = test == 2 ? (frame+1)*.25f : 1;
            glClearColor(red,.4,.6,alpha); glClear(GL_COLOR_BUFFER_BIT);
            /* Read and draw FBOs deliberately differ: the helper must copy the draw target. */
            glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
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
            for (int i=0;i<64;i++) for(int c=0;c<4;c++)
                if (abs(pixels[i*4+c]-(int)(expected[c]*255+.5))>2) errors++;
            GLenum error=glGetError(); if (error) errors++;
            printf("backdrop-test: shader=%d frame=%d pixel=%u,%u,%u,%u errors=%u gl=%#x\n",
                test,frame,pixels[0],pixels[1],pixels[2],pixels[3],errors,error);
        }
        glDeleteProgram(program);
    }
    OSMesaDestroyContext(ctx);
    return errors ? 1 : 0;
}
''')
subprocess.run(['clang', '-target', 'arm64-apple-ios16.0', '-isysroot', str(root / '.local/sdks/iPhoneOS16.5.sdk'),
                '-fuse-ld=lld', '-O2', '-I', str(root / '.local/mesa-25.0.7/include'),
                '-Wl,-rpath,@executable_path', '-Wl,-rpath,/redwood/share/gpu-bundle', str(generated),
                str(root / '.local/support/libOSMesa.8.dylib'), '-o', str(out)], check=True)
print(out)
