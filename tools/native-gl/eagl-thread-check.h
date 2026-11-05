#include <pthread.h>
static GLuint ng_eagl_thread_program;
static void *eagl_thread_worker(void *unused) {
  (void)unused;
  for (unsigned i = 0; i < 24; i++) {
    GLint linked = 0;
    glGetProgramiv(ng_eagl_thread_program, GL_LINK_STATUS, &linked);
    check(linked == 1, "EAGL worker sees the original context's program");
    glUseProgram(ng_eagl_thread_program);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    check(glGetError() == 0, "EAGL worker context GL status");
  }
  glClearColor(.25f, .5f, .75f,
               1); /* Last worker exits with an unflushed command. */
  return NULL;
}
static void eagl_thread_check(void *library) {
  if (!dlsym(library, "hakios_gl_native_enter"))
    return;
  GLuint
      vs = format_shader(
          GL_VERTEX_SHADER,
          "layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);}"),
      fs = format_shader(
          GL_FRAGMENT_SHADER,
          "precision highp float;out vec4 pixel;void main(){pixel=vec4(1);}");
  ng_eagl_thread_program = glCreateProgram();
  glAttachShader(ng_eagl_thread_program, vs);
  glAttachShader(ng_eagl_thread_program, fs);
  glLinkProgram(ng_eagl_thread_program);
  pthread_t workers[3];
  for (unsigned i = 0; i < 3; i++)
    check(!pthread_create(&workers[i], NULL, eagl_thread_worker, NULL),
          "EAGL worker creation");
  for (unsigned i = 0; i < 3; i++)
    check(!pthread_join(workers[i], NULL), "EAGL worker join");
  GLint linked = 0;
  glGetProgramiv(ng_eagl_thread_program, GL_LINK_STATUS, &linked);
  check(linked == 1 && glGetError() == 0,
        "original EAGL thread retains the same context");
  GLfloat colour[4] = {0};
  glGetFloatv(GL_COLOR_CLEAR_VALUE, colour);
  check(colour[0] == .25f && colour[1] == .5f && colour[2] == .75f &&
            colour[3] == 1,
        "deferred commands survive worker-thread exit");
  glUseProgram(0);
  glDeleteProgram(ng_eagl_thread_program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  puts("Native EAGL worker handoff PASSED (3 concurrent workers, shared "
       "program identity, original-thread return)");
}
