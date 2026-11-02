/* A producer deliberately killed while its host holds a completed GPU image,
 * and a separate observer that checks the retained surface/fence afterward. */
#define _POSIX_C_SOURCE 200809L
#include "readback.h"
#include <CoreFoundation/CoreFoundation.h>
#include <GLES3/gl3.h>
#include <IOSurface/IOSurfaceRef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define BGRA 0x80e1
static const unsigned width = 257, height = 131, row = 1088;
static void check(int ok, const char *reason) {
  if (!ok) {
    fprintf(stderr, "Native exit check FAILED: %s\n", reason);
    exit(1);
  }
}
static void number(CFMutableDictionaryRef d, CFStringRef key, int value) {
  CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &value);
  check(n != NULL, "surface property");
  CFDictionarySetValue(d, key, n);
  CFRelease(n);
}
int main(int argc, char **argv) {
  if (argc == 3 && !strcmp(argv[1], "--observe")) {
    char *end;
    unsigned long id = strtoul(argv[2], &end, 10);
    check(*argv[2] && !*end && id && id <= UINT32_MAX, "surface ID");
    IOSurfaceRef surface = IOSurfaceLookup((IOSurfaceID)id);
    check(surface != NULL, "surface survives original process exit");
    check(IOSurfaceGetWidth(surface) == width &&
              IOSurfaceGetHeight(surface) == height &&
              IOSurfaceGetBytesPerRow(surface) == row,
          "retained surface dimensions");
    uint32_t options = kIOSurfaceLockReadOnly | kIOSurfaceLockAvoidSync;
    check(IOSurfaceLock(surface, options, NULL) != 0,
          "CPU reader blocked before host completion");
    puts("Native exit observer: pending surface found after producer exit");
    fflush(stdout);
    check(IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL) == 0,
          "host completion unblocks surviving reader");
    uint8_t *pixels = IOSurfaceGetBaseAddress(surface);
    check(pixels != NULL, "observer mapped pixels");
    for (unsigned y = 0; y < height; y++)
      for (unsigned x = 0; x < row; x++) {
        int expected[] = {191, 128, 64, 255};
        int value = pixels[(size_t)y * row + x];
        check(x < width * 4 ? abs(value - expected[x % 4]) <= 1 : value == 0xa5,
              "fresh completed pixels and untouched padding");
      }
    IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL);
    CFRelease(surface);
    /* Once the observer releases, the host's hold must also disappear. */
    for (unsigned attempt = 0; attempt < 500; attempt++) {
      surface = IOSurfaceLookup((IOSurfaceID)id);
      if (!surface) {
        puts("Native shared process-exit check PASSED (retained, waited, "
             "pixels, released)");
        return 0;
      }
      CFRelease(surface);
      usleep(10000);
    }
    check(0, "surface released after host and observer completion");
  }
  check(argc == 1, "arguments");
  CFMutableDictionaryRef d =
      CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks);
  check(d != NULL, "surface dictionary");
  number(d, kIOSurfaceWidth, width);
  number(d, kIOSurfaceHeight, height);
  number(d, kIOSurfaceBytesPerElement, 4);
  number(d, kIOSurfaceBytesPerRow, row);
  number(d, kIOSurfacePixelFormat, 0x42475241);
  IOSurfaceRef surface = IOSurfaceCreate(d);
  CFRelease(d);
  check(surface && IOSurfaceGetBytesPerRow(surface) == row &&
            IOSurfaceGetAllocSize(surface) >= (size_t)row * height,
        "producer surface");
  uint8_t *pixels = IOSurfaceGetBaseAddress(surface);
  check(pixels && IOSurfaceLock(surface, 0, NULL) == 0, "producer lock");
  memset(pixels, 0xa5, IOSurfaceGetAllocSize(surface));
  IOSurfaceUnlock(surface, 0, NULL);
  GLuint texture, fbo;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
        "producer framebuffer");
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, row / 4);
  void *lease =
      ios_kvm_native_gl_readback_pin(pixels, (height - 1) * row + width * 4);
  check(lease != NULL, "producer pin");
  ios_kvm_native_gl_readback_surface(lease, IOSurfaceGetID(surface));
  ios_kvm_native_gl_readback_set(lease);
  printf("Native exit producer: pid=%d surface=%u\n", getpid(),
         IOSurfaceGetID(surface));
  fflush(stdout);
  glClearColor(.25f, .5f, .75f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glReadPixels(0, 0, width, height, BGRA, GL_UNSIGNED_BYTE, pixels);
  check(0, "producer must be killed during blocked shared read");
  return 1;
}
