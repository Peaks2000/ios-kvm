/* The real private EAGL IOSurface target path, including inverted render
 * targets. */
static void shared_eagl_surface(EAGLContext *context, void *library) {
  const char *mode = getenv("IOS_KVM_NATIVE_SHARED");
  if (!mode || strcmp(mode, "1"))
    return;
  uint64_t (*submissions)(void) =
      (void *)dlsym(library, "ios_kvm_native_gl_shared_submissions");
  check(submissions != NULL, "EAGL shared counter");
  for (unsigned inverted = 0; inverted < 2; inverted++) {
    NSDictionary *properties = @{
      (id)kIOSurfaceWidth : @257,
      (id)kIOSurfaceHeight : @131,
      (id)kIOSurfaceBytesPerElement : @4,
      (id)kIOSurfaceBytesPerRow : @1088,
      (id)kIOSurfacePixelFormat : @0x42475241
    };
    IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)properties);
    check(surface != NULL, "EAGL IOSurface");
    check(IOSurfaceGetBytesPerRow(surface) == 1088, "EAGL surface stride");
    uint8_t *pixels = IOSurfaceGetBaseAddress(surface);
    check(IOSurfaceLock(surface, 0, NULL) == 0, "EAGL initial surface lock");
    memset(pixels, 0xa5, IOSurfaceGetAllocSize(surface));
    IOSurfaceUnlock(surface, 0, NULL);
    GLuint texture, fbo;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    check([context attachImage:GL_TEXTURE_2D
                 toCoreSurface:surface
                invertedRender:inverted ? YES : NO],
          "private EAGL attachImage");
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture, 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "EAGL surface FBO");
    glViewport(0, 0, 257, 131);
    glDisable(GL_SCISSOR_TEST);
    GLuint vs = shader(GL_VERTEX_SHADER,
                       "#version 300 es\nlayout(location=0) in vec2 p;void "
                       "main(){gl_Position=vec4(p,0,1);}"),
           fs = shader(GL_FRAGMENT_SHADER,
                       "#version 300 es\nprecision highp float;uniform float "
                       "phase;out vec4 pixel;void "
                       "main(){pixel=vec4(phase,mod(floor(gl_FragCoord.x),256.)"
                       "/255.,mod(floor(gl_FragCoord.y),256.)/255.,1);}");
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    check(linked, "EAGL spatial program link");
    glUseProgram(program);
    GLint phase = glGetUniformLocation(program, "phase");
    check(phase >= 0, "EAGL surface phase");
    uint64_t before = submissions();
    for (unsigned frame = 0; frame < 9; frame++) {
      glUniform1f(phase, (frame + 1) / 16.0f);
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL);
      if (frame < 8)
        glFlush();
      else
        glFinish();
      check(IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL) == 0,
            "EAGL completed surface lock");
      for (unsigned y = 0; y < 131; y++)
        for (unsigned x = 0; x < 1088; x++) {
          uint8_t value = pixels[(size_t)y * 1088 + x];
          if (x < 257 * 4) {
            int expected[] = {(int)(inverted ? 130 - y : y),
                              (int)((x / 4) & 255),
                              ((frame + 1) * 255 + 8) / 16, 255};
            check(abs((int)value - expected[x % 4]) <= 1,
                  "EAGL shared surface spatial pixel");
          } else
            check(value == 0xa5, "EAGL surface padding");
        }
      IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL);
      check(glGetError() == 0, "EAGL shared GL status");
      usleep(2000);
    }
    check(submissions() - before == 8,
          "EAGL flush uses shared reads; Finish stays copied");
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &texture);
    glUseProgram(0);
    glDeleteProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    CFRelease(surface);
  }
  puts("Native EAGL shared IOSurface check PASSED (spatial/inverted targets, "
       "padding, Finish fallback)");
}
