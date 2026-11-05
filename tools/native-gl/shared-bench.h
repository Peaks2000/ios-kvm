/* Paired readback measurement, deliberately separate from desktop FPS. */
#include <time.h>
static uint64_t shared_bench_ns(void) {
  struct timespec t;
  require(clock_gettime(CLOCK_MONOTONIC, &t) == 0, "benchmark clock");
  return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static void shared_surface_benchmark(void) {
  const char *enabled = getenv("IOS_KVM_NATIVE_BENCH");
  if (!enabled || strcmp(enabled, "1"))
    return;
  const char *state_mode = getenv("IOS_KVM_NATIVE_BENCH_PIXEL_STATE");
  int state_bench = state_mode && !strcmp(state_mode, "1");
  const char *async_mode = getenv("IOS_KVM_NATIVE_BENCH_ASYNC_READBACK");
  int async_bench = async_mode && !strcmp(async_mode, "1");
  require(!state_bench || !async_bench, "one readback benchmark mode");
  const char *variable = async_bench ? "IOS_KVM_NATIVE_ASYNC_READBACK"
                                    : "IOS_KVM_NATIVE_STATE_CACHE";
  const char *original = getenv(variable);
  char *restore = original ? strdup(original) : NULL;
  require(!original || restore, "benchmark environment copy");
  const unsigned widths[] = {750, 1125}, heights[] = {1624, 2436};
  for (unsigned resolution = 0; resolution < 2; resolution++) {
    unsigned w = widths[resolution], h = heights[resolution];
    unsigned row = (w * 4 + 63) & ~63u;
    size_t span = (size_t)(h - 1) * row + w * 4;
    CFMutableDictionaryRef d =
        CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks);
    require(d != NULL, "benchmark surface dictionary");
    surface_number(d, kIOSurfaceWidth, w);
    surface_number(d, kIOSurfaceHeight, h);
    surface_number(d, kIOSurfaceBytesPerElement, 4);
    surface_number(d, kIOSurfaceBytesPerRow, row);
    surface_number(d, kIOSurfacePixelFormat, 0x42475241);
    IOSurfaceRef surface = IOSurfaceCreate(d);
    CFRelease(d);
    require(surface && IOSurfaceGetBytesPerRow(surface) == row &&
                IOSurfaceGetAllocSize(surface) >= (size_t)h * row,
            "benchmark surface layout");
    uint8_t *pixels = IOSurfaceGetBaseAddress(surface);
    require(pixels != NULL, "benchmark surface address");
    require(IOSurfaceLock(surface, 0, NULL) == 0, "benchmark initial lock");
    memset(pixels, 0xa5, IOSurfaceGetAllocSize(surface));
    IOSurfaceUnlock(surface, 0, NULL);
    void *lease = ios_kvm_native_gl_readback_pin(pixels, span);
    require(lease != NULL, "benchmark surface pin");
    GLuint tex = texture(w, h), fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           tex, 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "benchmark framebuffer");
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, row / 4);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    for (unsigned block = 0; block < 4; block++) {
      int middle = block == 1 || block == 2;
      int shared = state_bench || async_bench || middle;
      if (state_bench || async_bench)
        require(setenv(variable, middle ? "1" : "0", 1) == 0,
                "benchmark state mode");
      const char *current_mode = getenv("IOS_KVM_NATIVE_STATE_CACHE");
      int cached = !current_mode || strcmp(current_mode, "0");
      const char *current_async = getenv("IOS_KVM_NATIVE_ASYNC_READBACK");
      int asynchronous = shared && current_async && !strcmp(current_async, "1");
      ios_kvm_native_gl_readback_surface(lease,
                                         shared ? IOSurfaceGetID(surface) : 0);
      for (unsigned frame = 0; frame < 10; frame++) {
        require(IOSurfaceLock(surface, 0, NULL) == 0, "benchmark writer lock");
        unsigned red = 13 + block * 37 + frame;
        uint64_t count = ios_kvm_native_gl_shared_submissions();
        uint64_t async_count = ios_kvm_native_gl_async_submissions();
        void *previous = ios_kvm_native_gl_readback_set(lease);
        uint64_t start = shared_bench_ns();
        glClearColor(red / 255.0f, 61 / 255.0f, 173 / 255.0f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glReadPixels(0, 0, w, h, NG_BGRA, GL_UNSIGNED_BYTE, pixels);
        uint64_t elapsed = shared_bench_ns() - start;
        ios_kvm_native_gl_readback_set(previous);
        IOSurfaceUnlock(surface, 0, NULL);
        require(ios_kvm_native_gl_shared_submissions() - count ==
                    (unsigned)shared,
                "benchmark actual readback path");
        require(ios_kvm_native_gl_async_submissions() - async_count ==
                    (unsigned)asynchronous,
                "benchmark actual asynchronous submission path");
        require(IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL) == 0,
                "benchmark CPU reader lock");
        uint64_t completed = shared_bench_ns() - start;
        require(glGetError() == GL_NO_ERROR, "benchmark GPU status");
        /* Full image and padding validation is outside the measured interval.
         */
        for (unsigned y = 0; y < h; y++)
          for (unsigned x = 0; x < row; x++) {
            unsigned expected[] = {173, 61, red, 255};
            uint8_t actual = pixels[(size_t)y * row + x];
            require(x < w * 4 ? abs((int)actual - (int)expected[x % 4]) <= 1
                              : actual == 0xa5,
                    "benchmark fresh pixels and untouched padding");
          }
        IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL);
        printf("Native readback benchmark: {\"width\":%u,\"height\":%u,"
               "\"block\":%u,\"shared\":%s,\"state_cache\":%s,\"frame\":%u,"
               "\"warmup\":%s,\"asynchronous\":%s,"
               "\"elapsed_ns\":%llu,\"completed_ns\":%llu}\n",
               w, h, block, shared ? "true" : "false",
               cached ? "true" : "false", frame,
               frame < 2 ? "true" : "false",
               asynchronous ? "true" : "false", (unsigned long long)elapsed,
               (unsigned long long)completed);
        fflush(stdout);
        usleep(2000);
      }
    }
    ios_kvm_native_gl_readback_release(lease);
    CFRelease(surface);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
  }
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  if (state_bench || async_bench) {
    if (restore)
      require(setenv(variable, restore, 1) == 0,
              "restore benchmark environment");
    else
      require(unsetenv(variable) == 0,
              "restore unset benchmark environment");
  }
  free(restore);
  puts("Native shared readback benchmark PASSED (ABBA, every pixel checked)");
}
