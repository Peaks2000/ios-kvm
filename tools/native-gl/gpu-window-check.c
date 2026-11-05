/* Verify explicit DMA-BUF handoff into a native window's separate EGL display.
 * Software mode exercises the explicit CPU transfer fallback. */
#include "gpu-frame-fixture.h"
#include "gpu-dmabuf.h"
#include <SDL.h>

static void window_draw(EGLImageKHR image, unsigned source_width,
                         unsigned source_height, int drawable_width,
                         int drawable_height, unsigned number) {
  int width = drawable_width;
  int height = (int)((int64_t)width * source_height / source_width);
  if (height > drawable_height) {
    height = drawable_height;
    width = (int)((int64_t)height * source_width / source_height);
  }
  int left = (drawable_width - width) / 2;
  int bottom = (drawable_height - height) / 2;
  GLuint texture, vertex_array;
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC import_image =
      (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
  require(import_image != NULL, "window image import function");
  glActiveTexture(GL_TEXTURE0);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  import_image(GL_TEXTURE_2D, image);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  GLuint vertex = shader(GL_VERTEX_SHADER,
      "#version 300 es\nout vec2 uv;void main(){const vec2 p[4]=vec2[4]("
      "vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1));"
      "gl_Position=vec4(p[gl_VertexID],0,1);uv=p[gl_VertexID]*0.5+0.5;"
      "uv.y=1.0-uv.y;}");
  GLuint fragment = shader(GL_FRAGMENT_SHADER,
      "#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D image;"
      "out vec4 color;void main(){color=texture(image,uv);}");
  GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  GLint linked;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  require(linked, "window program link");
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "image"), 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(left, bottom, width, height);
  glGenVertexArrays(1, &vertex_array);
  glBindVertexArray(vertex_array);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  /* The preview interprets row zero as the phone's top, so y is reversed. */
  for (unsigned quadrant = 0; quadrant < 4; quadrant++) {
    unsigned x = left + width * ((quadrant % 2) * 2 + 1) / 4;
    unsigned y = bottom + height * ((quadrant / 2) * 2 + 1) / 4;
    uint8_t pixel[4], expected[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    pattern_color(number, quadrant ^ 2, expected);
    if (memcmp(pixel, expected, 3)) {
      EGLint surface_width = 0, surface_height = 0;
      eglQuerySurface(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW),
                       EGL_WIDTH, &surface_width);
      eglQuerySurface(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW),
                       EGL_HEIGHT, &surface_height);
      fprintf(stderr, "window mismatch at %u,%u actual=%u,%u,%u expected=%u,%u,%u "
              "drawable=%dx%d EGL=%dx%d\n", x, y, pixel[0], pixel[1], pixel[2],
              expected[0], expected[1], expected[2], drawable_width,
              drawable_height, surface_width, surface_height);
    }
    require(!memcmp(pixel, expected, 3), "visible window scaled quadrant pixels");
  }
  if (left > 0 || bottom > 0) {
    uint8_t pixel[4];
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    require(!pixel[0] && !pixel[1] && !pixel[2], "letterbox stays black");
  }
  glDeleteProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  glDeleteTextures(1, &texture);
  glDeleteVertexArrays(1, &vertex_array);
  require(glGetError() == GL_NO_ERROR, "window rendering GL status");
  printf("Native GPU window pixels PASSED (%dx%d, viewport %dx%d at %d,%d)\n",
         drawable_width, drawable_height, width, height, left, bottom);
}
int main(int argc, char **argv) {
  int software = 0, linger = 0, large = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--software"))
      software = 1;
    else if (!strcmp(argv[i], "--linger"))
      linger = 1;
    else if (!strcmp(argv[i], "--large"))
      large = 1;
    else {
      fprintf(stderr, "usage: gpu-window-check [--software] [--linger] [--large]\n");
      return 2;
    }
  }
  ng_display = ng_choose_display();
  require(ng_display != EGL_NO_DISPLAY && eglInitialize(ng_display, NULL, NULL) &&
          eglBindAPI(EGL_OPENGL_ES_API), "producer EGL display");
  const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  EGLint count;
  require(eglChooseConfig(ng_display, attributes, &ng_config, 1, &count) && count,
          "producer EGL config");
  struct test_context producer = context_create();
  context_current(producer);
  char *renderer = strdup((const char *)glGetString(GL_RENDERER));
  require(renderer != NULL, "save producer renderer");
  printf("Native window producer renderer: %s\n", renderer);
  require(!ng_gpu_frames_init(ng_display, 1), "GPU snapshot capabilities");
  const unsigned width = large ? 1125 : 750, height = large ? 2436 : 1624, number = 3;
  target_create(width, height);
  pattern_draw(width, height, number);
  uint8_t *pixels = NULL;
  if (software) {
    pixels = malloc((size_t)width * height * 4);
    require(pixels != NULL, "software fallback pixel allocation");
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    require(glGetError() == GL_NO_ERROR, "software fallback readback");
  }
  require(ng_gpu_frame_capture(0, 0, width, height, 0x1000, 1), "capture GPU image");
  struct ng_gpu_frame *frame = ng_gpu_frame_acquire(ng_gpu_owner, 0x1000, 1);
  require(frame != NULL, "hold GPU frame lease");
  struct ng_gpu_dmabuf buffer;
  enum ng_dma_result status;
  for (unsigned attempt = 0;; attempt++) {
    status = ng_gpu_dmabuf_export(frame, &buffer);
    if (status != NG_DMA_PENDING)
      break;
    require(attempt < 5000, "source GPU fence completes");
    poll(NULL, 0, 1);
  }
  if (!software)
    require(status == NG_DMA_READY, "GPU DMA-BUF export");
  pattern_draw(width, height, 9);
  ng_gpu_frames_owner_close();
  require(!SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_Window *window = SDL_CreateWindow("ios-kvm GPU transport check",
      SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 375, 812,
      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  require(window != NULL, SDL_GetError());
  SDL_GLContext consumer = SDL_GL_CreateContext(window);
  require(consumer != NULL, SDL_GetError());
  SDL_GL_SetSwapInterval(0);
  EGLDisplay destination = eglGetCurrentDisplay();
  require(destination != ng_display && destination != EGL_NO_DISPLAY,
          "native window uses a separate EGL display");
  require(!strcmp(renderer, (const char *)glGetString(GL_RENDERER)),
          "window and producer use the selected renderer");
  printf("Native window consumer renderer: %s (%s)\n", glGetString(GL_RENDERER),
         SDL_GetCurrentVideoDriver());
  context_destroy(producer);
  EGLImageKHR image;
  if (software) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    free(pixels);
    const EGLint preserve[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    image = ng_gpu.create_image(destination, eglGetCurrentContext(),
        EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)(uintptr_t)texture, preserve);
    glDeleteTextures(1, &texture);
    puts("Native window transfer: explicit software CPU fallback");
  } else {
    image = ng_gpu_dmabuf_import(destination, &buffer);
    printf("Native window transfer: DMA-BUF planes=%d fourcc=%#x modifier=%#llx\n",
           buffer.planes, buffer.fourcc, (unsigned long long)buffer.modifiers[0]);
  }
  ng_gpu_dmabuf_close(&buffer);
  require(image != EGL_NO_IMAGE_KHR, "destination image creation");
  frame_verify_image(image, width, height, number);
  const int sizes[][2] = {{375, 812}, {640, 640}, {300, 900}};
  for (unsigned i = 0; i < 3; i++) {
    SDL_SetWindowSize(window, sizes[i][0], sizes[i][1]);
    for (unsigned event = 0; event < 20; event++) {
      SDL_PumpEvents();
      SDL_Delay(5);
    }
    /* Apply the native surface resize before validating the new backbuffer. */
    SDL_GL_SwapWindow(window);
    int drawable_width, drawable_height;
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    require(drawable_width > 0 && drawable_height > 0, "window drawable size");
    EGLint surface_width, surface_height;
    require(eglQuerySurface(destination, eglGetCurrentSurface(EGL_DRAW), EGL_WIDTH,
                             &surface_width) &&
            eglQuerySurface(destination, eglGetCurrentSurface(EGL_DRAW), EGL_HEIGHT,
                             &surface_height) && surface_width == drawable_width &&
            surface_height == drawable_height, "SDL and EGL agree on drawable dimensions");
    window_draw(image, width, height, drawable_width, drawable_height, number);
    SDL_GL_SwapWindow(window);
    if (linger)
      SDL_Delay(700);
  }
  glFinish();
  require(ng_gpu.destroy_image(destination, image), "release imported GPU image");
  ng_gpu_frame_release(frame);
  require(!ng_gpu_frame_stats_read().live_frames, "GPU frame lease cleanup");
  SDL_GL_DeleteContext(consumer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  eglTerminate(ng_display);
  free(renderer);
  puts("Native cross-display window check PASSED (all source pixels, GPU lifetime, "
       "scaled window, letterboxing, orientation and FD cleanup)");
  return 0;
}
