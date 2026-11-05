#include "readback.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurfaceRef.h>
#include <unistd.h>
static void surface_number(CFMutableDictionaryRef d, CFStringRef key,
                           int value) {
  CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &value);
  require(n != NULL, "surface number");
  CFDictionarySetValue(d, key, n);
  CFRelease(n);
}
static void shared_surface_check(void) {
  const char *mode = getenv("IOS_KVM_NATIVE_SHARED");
  if (!mode || strcmp(mode, "1"))
    return;
  const unsigned w = 257, h = 131, row = 1088;
  CFMutableDictionaryRef d =
      CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks);
  require(d != NULL, "surface dictionary");
  surface_number(d, kIOSurfaceWidth, w);
  surface_number(d, kIOSurfaceHeight, h);
  surface_number(d, kIOSurfaceBytesPerElement, 4);
  surface_number(d, kIOSurfaceBytesPerRow, row);
  surface_number(d, kIOSurfacePixelFormat, 0x42475241);
  IOSurfaceRef surface = IOSurfaceCreate(d);
  CFRelease(d);
  require(surface != NULL, "shared IOSurface");
  require(IOSurfaceGetBytesPerRow(surface) == row, "surface row stride");
  uint8_t *pixels = IOSurfaceGetBaseAddress(surface);
  size_t span = (h - 1) * row + w * 4;
  require(pixels && IOSurfaceGetAllocSize(surface) >= span, "surface storage");
  require(IOSurfaceLock(surface, 0, NULL) == 0, "initial surface lock");
  memset(pixels, 0xa5, IOSurfaceGetAllocSize(surface));
  IOSurfaceUnlock(surface, 0, NULL);
  GLuint texture_id = texture(w, h), fbo;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture_id, 0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "shared FBO");
  glDisable(GL_SCISSOR_TEST);
  glViewport(0, 0, w, h);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, row / 4);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  GLint previous_program = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
  GLuint
      vs = shader(
          GL_VERTEX_SHADER,
          "layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);}"),
      fs = shader(
          GL_FRAGMENT_SHADER,
          "precision highp float;uniform float phase;out vec4 pixel;void "
          "main(){pixel=vec4(phase,mod(floor(gl_FragCoord.x),256.)/"
          "255.,mod(floor(gl_FragCoord.y),256.)/255.,1);}");
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  require(linked, "shared spatial shader link");
  glUseProgram(program);
  GLint phase = glGetUniformLocation(program, "phase");
  require(phase >= 0, "shared phase uniform");
  void *lease = ios_kvm_native_gl_readback_pin(pixels, span);
  require(lease != NULL, "explicit surface pin");
  uint64_t before = ios_kvm_native_gl_shared_submissions();
  uint64_t async_before = ios_kvm_native_gl_async_submissions();
  for (unsigned frame = 0; frame < 12; frame++) {
    require(IOSurfaceLock(surface, 0, NULL) == 0, "surface writer lock");
    glUniform1f(phase, (frame + 1) / 16.0f);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL);
    if (frame == 3)
      glEnable(
          0xdeadbeef); /* Shared readback must preserve an earlier GL error. */
    if (frame == 4) {
      glPixelStorei(GL_PACK_ALIGNMENT, 3);
      glPixelStorei(GL_PACK_ROW_LENGTH, -1);
    }
    ios_kvm_native_gl_readback_surface(lease, IOSurfaceGetID(surface));
    void *old = ios_kvm_native_gl_readback_set(lease);
    glReadPixels(0, 0, w, h, NG_BGRA, GL_UNSIGNED_BYTE, pixels);
    ios_kvm_native_gl_readback_set(old);
    IOSurfaceUnlock(surface, 0, NULL);
    require(IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL) == 0,
            "completed surface CPU lock");
    for (unsigned y = 0; y < h; y++)
      for (unsigned x = 0; x < row; x++) {
        uint8_t value = pixels[(size_t)y * row + x];
        if (x < w * 4) {
          int expected[] = {(int)(y & 255), (int)((x / 4) & 255),
                            ((frame + 1) * 255 + 8) / 16, 255};
          require(abs((int)value - expected[x % 4]) <= 1,
                  "shared spatial pixel");
        } else
          require(value == 0xa5, "shared surface padding");
      }
    IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL);
    require(glGetError() == (frame == 3   ? GL_INVALID_ENUM
                             : frame == 4 ? GL_INVALID_VALUE
                                          : GL_NO_ERROR),
            "shared read preserves prior GL errors");
    require(glGetError() == 0, "shared GL error consumed once");
    usleep(2000);
  }
  uint64_t submitted = ios_kvm_native_gl_shared_submissions() - before;
  require(submitted == 12, "twelve actual retained shared reads");
  const char *async_mode = getenv("IOS_KVM_NATIVE_ASYNC_READBACK");
  int asynchronous = async_mode && !strcmp(async_mode, "1");
  require(ios_kvm_native_gl_async_submissions() - async_before ==
              (asynchronous ? submitted : 0),
          "actual synchronous/asynchronous retained submission mode");
  /* An active but untagged lease must still use ordinary synchronous readback.
   */
  ios_kvm_native_gl_readback_surface(lease, 0);
  void *old = ios_kvm_native_gl_readback_set(lease);
  glReadPixels(0, 0, w, h, NG_BGRA, GL_UNSIGNED_BYTE, pixels);
  ios_kvm_native_gl_readback_set(old);
  require(ios_kvm_native_gl_shared_submissions() - before == submitted,
          "untagged lease stays copied");
  ios_kvm_native_gl_readback_release(lease);
  CFRelease(surface);
  CFMutableDictionaryRef error_dict =
      CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks);
  surface_number(error_dict, kIOSurfaceWidth, w);
  surface_number(error_dict, kIOSurfaceHeight, h);
  surface_number(error_dict, kIOSurfaceBytesPerElement, 4);
  surface_number(error_dict, kIOSurfaceBytesPerRow, row);
  surface_number(error_dict, kIOSurfacePixelFormat, 0x42475241);
  IOSurfaceRef error_surface = IOSurfaceCreate(error_dict);
  CFRelease(error_dict);
  require(error_surface != NULL, "error test surface");
  uint8_t *error_pixels = IOSurfaceGetBaseAddress(error_surface);
  require(IOSurfaceLock(error_surface, 0, NULL) == 0, "error surface lock");
  memset(error_pixels, 0xa5, IOSurfaceGetAllocSize(error_surface));
  IOSurfaceUnlock(error_surface, 0, NULL);
  lease = ios_kvm_native_gl_readback_pin(error_pixels, span);
  require(lease != NULL, "error test pin");
  ios_kvm_native_gl_readback_surface(lease, IOSurfaceGetID(error_surface));
  GLuint incomplete;
  glGenFramebuffers(1, &incomplete);
  glBindFramebuffer(GL_FRAMEBUFFER, incomplete);
  old = ios_kvm_native_gl_readback_set(lease);
  glReadPixels(0, 0, w, h, NG_BGRA, GL_UNSIGNED_BYTE, error_pixels);
  ios_kvm_native_gl_readback_set(old);
  require(ios_kvm_native_gl_shared_submissions() - before == submitted + 1,
          "failed read retains and completes kernel hold");
  require(glGetError() == GL_INVALID_FRAMEBUFFER_OPERATION,
          "shared read preserves new GPU error");
  require(glGetError() == 0, "shared GPU error consumed once");
  for (size_t i = 0; i < IOSurfaceGetAllocSize(error_surface); i++)
    require(error_pixels[i] == 0xa5, "GPU error leaves destination unchanged");
  require(IOSurfaceLock(error_surface, kIOSurfaceLockReadOnly, NULL) != 0,
          "kernel observes shared error completion");
  ios_kvm_native_gl_readback_release(lease);
  CFRelease(error_surface);
  glDeleteFramebuffers(1, &incomplete);
  glUseProgram(previous_program);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);

  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &texture_id);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  puts("Native shared IOSurface check PASSED (12 retained reads, padded "
       "pixels, untagged fallback, GL error completion)");
}
