/* Descriptor ownership and four-plane metadata checks without a GPU or VM. */
#define main ng_server_main
#include "server.c"
#undef main
#include <assert.h>

static const char *dma_extensions = "EGL_MESA_image_dma_buf_export "
    "EGL_EXT_image_dma_buf_import EGL_EXT_image_dma_buf_import_modifiers";
static int plane_count = 4, ready = 1, export_fails, shared_fd = 1;
static int pipe_fds[2], last_exported = -1, queries, imports;
static int exported_fds[4];
static EGLint captured_attributes[47];
static EGLint mock_wait(EGLDisplay display, EGLSyncKHR fence, EGLint flags,
                         EGLTimeKHR timeout) {
  assert(display == (EGLDisplay)1 && fence == (EGLSyncKHR)2);
  assert(!flags && !timeout);
  return ready ? EGL_CONDITION_SATISFIED_KHR : EGL_TIMEOUT_EXPIRED_KHR;
}
static const char *mock_extensions(EGLDisplay display, EGLint name) {
  assert((display == (EGLDisplay)1 || display == (EGLDisplay)4) &&
         name == EGL_EXTENSIONS);
  return dma_extensions;
}
static EGLBoolean mock_query(EGLDisplay display, EGLImageKHR image, int *fourcc,
                              int *planes, EGLuint64KHR *modifiers) {
  assert(display == (EGLDisplay)1 && image == (EGLImageKHR)3);
  queries++;
  if (fourcc)
    *fourcc = 0x34324241;
  if (planes)
    *planes = plane_count;
  if (modifiers)
    for (int i = 0; i < plane_count; i++)
      modifiers[i] = UINT64_C(0x0300000000123456) + i;
  return EGL_TRUE;
}
static EGLBoolean mock_export(EGLDisplay display, EGLImageKHR image, int *fds,
                               EGLint *strides, EGLint *offsets) {
  assert(display == (EGLDisplay)1 && image == (EGLImageKHR)3);
  for (int i = 0; i < plane_count; i++) {
    if ((shared_fd == 1 && i > 0) || (shared_fd == 2 && i > 1))
      fds[i] = -1;
    else
      fds[i] = dup(pipe_fds[0]);
    exported_fds[i] = fds[i];
    strides[i] = 4096 + i * 128;
    offsets[i] = i * 512;
    if (!i) {
      last_exported = fds[i];
      if (export_fails)
        return EGL_FALSE;
    }
  }
  return EGL_TRUE;
}
static EGLImageKHR mock_import(EGLDisplay display, EGLContext context,
                                EGLenum target, EGLClientBuffer buffer,
                                const EGLint *attributes) {
  assert(display == (EGLDisplay)4 && context == EGL_NO_CONTEXT &&
         target == EGL_LINUX_DMA_BUF_EXT && buffer == NULL);
  unsigned i = 0;
  while (attributes[i] != EGL_NONE) {
    assert(i + 2 < 47);
    captured_attributes[i] = attributes[i];
    captured_attributes[i + 1] = attributes[i + 1];
    i += 2;
  }
  captured_attributes[i] = EGL_NONE;
  imports++;
  return (EGLImageKHR)5;
}
static __eglMustCastToProperFunctionPointerType mock_proc(const char *name) {
  if (!strcmp(name, "eglExportDMABUFImageQueryMESA"))
    return (void *)mock_query;
  if (!strcmp(name, "eglExportDMABUFImageMESA"))
    return (void *)mock_export;
  assert(!strcmp(name, "eglCreateImageKHR"));
  return (void *)mock_import;
}
#define eglQueryString mock_extensions
#define eglGetProcAddress mock_proc
#include "gpu-dmabuf.h"
#undef eglQueryString
#undef eglGetProcAddress

static EGLint attribute(EGLint key) {
  for (unsigned i = 0; captured_attributes[i] != EGL_NONE; i += 2)
    if (captured_attributes[i] == key)
      return captured_attributes[i + 1];
  assert(!"missing import attribute");
  return 0;
}
int main(void) {
  assert(!pipe(pipe_fds));
  ng_gpu.display = (EGLDisplay)1;
  ng_gpu.client_wait = mock_wait;
  struct ng_gpu_frame frame = {.image = (EGLImageKHR)3, .fence = (EGLSyncKHR)2,
                                .width = 257, .height = 131};
  struct ng_gpu_dmabuf buffer;
  ready = 0;
  assert(ng_gpu_dmabuf_export(&frame, &buffer) == NG_DMA_PENDING && !queries);
  ng_gpu_dmabuf_close(&buffer);
  ready = 1;
  plane_count = 5;
  assert(ng_gpu_dmabuf_export(&frame, &buffer) == NG_DMA_UNAVAILABLE);
  assert(queries == 1); /* No modifier array is supplied for excessive planes. */
  plane_count = 4;
  for (shared_fd = 0; shared_fd <= 1; shared_fd++) {
    assert(ng_gpu_dmabuf_export(&frame, &buffer) == NG_DMA_READY);
    for (int i = 0; i < 4; i++) {
      assert(buffer.fds[i] >= 0 && (fcntl(buffer.fds[i], F_GETFD) & FD_CLOEXEC));
      assert(buffer.strides[i] == 4096 + i * 128 && buffer.offsets[i] == i * 512);
      if (shared_fd)
        assert(buffer.fds[i] == buffer.fds[0]);
    }
    assert(ng_gpu_dmabuf_import((EGLDisplay)4, &buffer) == (EGLImageKHR)5);
    assert(attribute(EGL_WIDTH) == 257 && attribute(EGL_HEIGHT) == 131 &&
           attribute(EGL_LINUX_DRM_FOURCC_EXT) == 0x34324241);
    const EGLint fd_keys[] = {EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT,
        EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE3_FD_EXT};
    const EGLint offset_keys[] = {EGL_DMA_BUF_PLANE0_OFFSET_EXT,
        EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT,
        EGL_DMA_BUF_PLANE3_OFFSET_EXT};
    const EGLint pitch_keys[] = {EGL_DMA_BUF_PLANE0_PITCH_EXT,
        EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE2_PITCH_EXT,
        EGL_DMA_BUF_PLANE3_PITCH_EXT};
    for (unsigned i = 0; i < 4; i++) {
      assert(attribute(fd_keys[i]) == buffer.fds[i]);
      assert(attribute(offset_keys[i]) == (EGLint)i * 512);
      assert(attribute(pitch_keys[i]) == 4096 + (EGLint)i * 128);
      EGLint low = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT + i * 2;
      assert((uint32_t)attribute(low) == 0x123456 + i && attribute(low + 1) == 0x03000000);
    }
    int saved = buffer.fds[0];
    ng_gpu_dmabuf_close(&buffer);
    assert(fcntl(saved, F_GETFD) == -1 && errno == EBADF);
    for (unsigned i = 0; i < 4; i++)
      assert(buffer.fds[i] == -1);
    ng_gpu_dmabuf_close(&buffer);
  }
  assert(ng_gpu_dmabuf_export(&frame, &buffer) == NG_DMA_UNAVAILABLE);
  assert(fcntl(exported_fds[0], F_GETFD) == -1 && errno == EBADF);
  assert(fcntl(exported_fds[1], F_GETFD) == -1 && errno == EBADF);
  shared_fd = 1;
  export_fails = 1;
  assert(ng_gpu_dmabuf_export(&frame, &buffer) == NG_DMA_UNAVAILABLE);
  assert(fcntl(last_exported, F_GETFD) == -1 && errno == EBADF);
  export_fails = 0;
  assert(ng_gpu_dmabuf_export(&frame, &buffer) == NG_DMA_READY);
  dma_extensions = "EGL_EXT_image_dma_buf_import";
  assert(ng_gpu_dmabuf_import((EGLDisplay)4, &buffer) == EGL_NO_IMAGE_KHR);
  buffer.planes = 3;
  for (unsigned i = 0; i < 4; i++)
    buffer.modifiers[i] = NG_DMA_MOD_INVALID;
  assert(ng_gpu_dmabuf_import((EGLDisplay)4, &buffer) == (EGLImageKHR)5);
  buffer.width = 0;
  assert(ng_gpu_dmabuf_import((EGLDisplay)4, &buffer) == EGL_NO_IMAGE_KHR);
  ng_gpu_dmabuf_close(&buffer);
  assert(fcntl(pipe_fds[0], F_GETFD) >= 0 && fcntl(pipe_fds[1], F_GETFD) >= 0);
  close(pipe_fds[0]);
  close(pipe_fds[1]);
  assert(imports == 3);
  puts("Native DMA-BUF metadata check PASSED (nonblocking readiness, four planes, "
       "modifiers, shared/distinct FDs, bounds, partial failure and cleanup)");
  return 0;
}
