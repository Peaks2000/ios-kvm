/* Explicit image transport across EGL displays. Export only signaled frames,
 * using a zero-timeout fence query. The caller keeps its frame lease until the
 * imported image and consuming GPU work retire. Returned FDs are caller-owned. */
#ifndef IOS_KVM_NATIVE_GPU_DMABUF_H
#define IOS_KVM_NATIVE_GPU_DMABUF_H
#include "gpu-frames.h"
#include <fcntl.h>
#include <unistd.h>

#define NG_DMA_MOD_INVALID UINT64_C(0x00ffffffffffffff)
enum ng_dma_result { NG_DMA_UNAVAILABLE = -1, NG_DMA_PENDING = 0, NG_DMA_READY = 1 };
struct ng_gpu_dmabuf {
  EGLint width, height, fourcc, planes;
  int fds[4];
  EGLint strides[4], offsets[4];
  EGLuint64KHR modifiers[4];
};
static void ng_gpu_dmabuf_close(struct ng_gpu_dmabuf *buffer) {
  for (unsigned i = 0; i < 4; i++) {
    int fd = buffer->fds[i];
    if (fd < 0)
      continue;
    for (unsigned j = i; j < 4; j++)
      if (buffer->fds[j] == fd)
        buffer->fds[j] = -1;
    close(fd);
  }
}
static enum ng_dma_result ng_gpu_dmabuf_export(struct ng_gpu_frame *frame,
                                               struct ng_gpu_dmabuf *buffer) {
  *buffer = (struct ng_gpu_dmabuf){.fds = {-1, -1, -1, -1}};
  if (!frame || !ng_gpu_extension(eglQueryString(ng_gpu.display, EGL_EXTENSIONS),
                                 "EGL_MESA_image_dma_buf_export"))
    return NG_DMA_UNAVAILABLE;
  EGLint ready = ng_gpu.client_wait(ng_gpu.display, frame->fence, 0, 0);
  if (ready == EGL_TIMEOUT_EXPIRED_KHR)
    return NG_DMA_PENDING;
  if (ready != EGL_CONDITION_SATISFIED_KHR)
    return NG_DMA_UNAVAILABLE;
  PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC query =
      (void *)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
  PFNEGLEXPORTDMABUFIMAGEMESAPROC export_image =
      (void *)eglGetProcAddress("eglExportDMABUFImageMESA");
  if (!query || !export_image ||
      !query(ng_gpu.display, frame->image, &buffer->fourcc, &buffer->planes,
              NULL) || buffer->planes < 1 || buffer->planes > 4 ||
      !query(ng_gpu.display, frame->image, NULL, NULL, buffer->modifiers))
    return NG_DMA_UNAVAILABLE;
  buffer->width = frame->width;
  buffer->height = frame->height;
  if (!export_image(ng_gpu.display, frame->image, buffer->fds, buffer->strides,
                     buffer->offsets)) {
    ng_gpu_dmabuf_close(buffer);
    return NG_DMA_UNAVAILABLE;
  }
  /* A single exported FD can back multiple planes. With multiple distinct FDs,
   * missing plane-to-FD mappings are ambiguous and require another path. */
  unsigned supplied = 0;
  int only_fd = -1;
  for (EGLint i = 0; i < buffer->planes; i++)
    if (buffer->fds[i] >= 0 && buffer->fds[i] != only_fd) {
      only_fd = buffer->fds[i];
      supplied++;
    }
  for (EGLint i = 0; i < buffer->planes; i++) {
    if (buffer->fds[i] < 0 && supplied == 1)
      buffer->fds[i] = only_fd;
    int flags = fcntl(buffer->fds[i], F_GETFD);
    if (buffer->strides[i] <= 0 || buffer->offsets[i] < 0 || flags < 0 ||
        fcntl(buffer->fds[i], F_SETFD, flags | FD_CLOEXEC)) {
      ng_gpu_dmabuf_close(buffer);
      return NG_DMA_UNAVAILABLE;
    }
  }
  return NG_DMA_READY;
}
/* Import borrows the descriptors. EGL retains its own references to storage.
 * Four-plane layouts and explicit modifiers are retained without substitution. */
static EGLImageKHR ng_gpu_dmabuf_import(EGLDisplay display,
                                         const struct ng_gpu_dmabuf *buffer) {
  const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
  if (!ng_gpu_extension(extensions, "EGL_EXT_image_dma_buf_import") ||
      buffer->width <= 0 || buffer->height <= 0 || buffer->width > 4096 ||
      buffer->height > 4096 || buffer->planes < 1 || buffer->planes > 4)
    return EGL_NO_IMAGE_KHR;
  const EGLint fd_attributes[] = {EGL_DMA_BUF_PLANE0_FD_EXT,
      EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE3_FD_EXT};
  const EGLint offset_attributes[] = {EGL_DMA_BUF_PLANE0_OFFSET_EXT,
      EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT,
      EGL_DMA_BUF_PLANE3_OFFSET_EXT};
  const EGLint pitch_attributes[] = {EGL_DMA_BUF_PLANE0_PITCH_EXT,
      EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE2_PITCH_EXT,
      EGL_DMA_BUF_PLANE3_PITCH_EXT};
  const EGLint modifier_low[] = {EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
      EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
      EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT};
  const EGLint modifier_high[] = {EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
      EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT,
      EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT};
  EGLint attributes[7 + 4 * 10] = {EGL_WIDTH, buffer->width, EGL_HEIGHT,
      buffer->height, EGL_LINUX_DRM_FOURCC_EXT, buffer->fourcc};
  unsigned used = 6;
  int explicit_modifiers = ng_gpu_extension(extensions,
                                           "EGL_EXT_image_dma_buf_import_modifiers");
  if (buffer->planes == 4 && !explicit_modifiers)
    return EGL_NO_IMAGE_KHR;
  for (EGLint i = 0; i < buffer->planes; i++) {
    if (buffer->fds[i] < 0 || buffer->strides[i] <= 0 || buffer->offsets[i] < 0)
      return EGL_NO_IMAGE_KHR;
    attributes[used++] = fd_attributes[i];
    attributes[used++] = buffer->fds[i];
    attributes[used++] = offset_attributes[i];
    attributes[used++] = buffer->offsets[i];
    attributes[used++] = pitch_attributes[i];
    attributes[used++] = buffer->strides[i];
    if (buffer->modifiers[i] != NG_DMA_MOD_INVALID) {
      if (!explicit_modifiers)
        return EGL_NO_IMAGE_KHR;
      attributes[used++] = modifier_low[i];
      attributes[used++] = (EGLint)(uint32_t)buffer->modifiers[i];
      attributes[used++] = modifier_high[i];
      attributes[used++] = (EGLint)(uint32_t)(buffer->modifiers[i] >> 32);
    }
  }
  attributes[used] = EGL_NONE;
  PFNEGLCREATEIMAGEKHRPROC create_image = (void *)eglGetProcAddress("eglCreateImageKHR");
  if (!create_image)
    return EGL_NO_IMAGE_KHR;
  return create_image(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL,
                       attributes);
}
#endif
