/* EXT labels have no native KHR length limit. Keep their full state here and
 * mirror a bounded version into KHR/core labels when that capability exists. */
struct ng_label {
  struct ng_label *next;
  GLenum type;
  GLuint object;
  size_t length;
  char *text;
};
static _Thread_local struct ng_label *ng_labels;
static _Thread_local size_t ng_label_bytes, ng_label_count;
static _Thread_local int ng_label_mirror_ready;
static _Thread_local GLint ng_label_mirror_limit;
static _Thread_local void (*ng_label_mirror)(GLenum, GLuint, GLsizei,
                                             const GLchar *);
static GLenum ng_label_core_type(GLenum type) {
  switch (type) {
  case 0x9151:
    return 0x82e0; /* BUFFER */
  case 0x8b48:
    return 0x82e1; /* SHADER */
  case 0x8b40:
    return 0x82e2; /* PROGRAM */
  case 0x9154:
    return 0x8074; /* VERTEX_ARRAY */
  case 0x9153:
    return 0x82e3; /* QUERY */
  case 0x8a4f:
    return 0x82e4; /* PROGRAM_PIPELINE */
  case 0x1702:
  case 0x8d40:
  case 0x8d41:
  case 0x82e6:
  case 0x8e22:
    return type;
  default:
    return 0;
  }
}
static int ng_label_exists(GLenum type, GLuint object) {
  switch (type) {
  case 0x9151:
    return glIsBuffer(object);
  case 0x8b48:
    return glIsShader(object);
  case 0x8b40:
    return glIsProgram(object);
  case 0x9154:
    return glIsVertexArray(object);
  case 0x9153:
    return glIsQuery(object);
  case 0x8a4f:
    return glIsProgramPipeline(object);
  case 0x1702:
    return glIsTexture(object);
  case 0x8d40:
    return glIsFramebuffer(object);
  case 0x8d41:
    return glIsRenderbuffer(object);
  case 0x82e6:
    return glIsSampler(object);
  case 0x8e22:
    return glIsTransformFeedback(object);
  default:
    return 0;
  }
}
static void ng_label_drop(struct ng_label **at) {
  struct ng_label *entry = *at;
  *at = entry->next;
  ng_label_bytes -= entry->length;
  ng_label_count--;
  free(entry->text);
  free(entry);
}
static void ng_label_forget(GLenum type, GLuint object) {
  struct ng_label **at = &ng_labels;
  while (*at) {
    if ((*at)->type == type && (*at)->object == object)
      ng_label_drop(at);
    else
      at = &(*at)->next;
  }
}
static void ng_labels_cleanup(void) {
  while (ng_labels)
    ng_label_drop(&ng_labels);
}
static void ng_labels_objects(int generate, uint64_t kind, GLsizei n,
                              GLuint *names) {
  const GLenum types[] = {0,      0x9151, 0x1702, 0x8d40, 0x8d41,
                          0x9154, 0x82e6, 0x9153, 0x8e22};
  (void)generate;
  if (kind > 0 && kind < sizeof(types) / sizeof(types[0]))
    for (GLsizei i = 0; i < n; i++)
      ng_label_forget(types[kind], names[i]);
}
static void ng_label_native(GLenum type, GLuint object, const char *text,
                            size_t n) {
  if (!ng_label_mirror_ready) {
    ng_label_mirror_ready = 1;
    int major = 0, minor = 0, available = 0;
    const char *version = (const char *)glGetString(GL_VERSION);
    if (version && sscanf(version, "OpenGL ES %d.%d", &major, &minor) == 2)
      available = major > 3 || (major == 3 && minor >= 2);
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; !available && i < count; i++) {
      const char *extension = (const char *)glGetStringi(GL_EXTENSIONS, i);
      if (extension && !strcmp(extension, "GL_KHR_debug"))
        available = 1;
    }
    if (available) {
      glGetIntegerv(0x82e8, &ng_label_mirror_limit);
      ng_label_mirror =
          (void (*)(GLenum, GLuint, GLsizei, const GLchar *))eglGetProcAddress(
              "glObjectLabel");
      if (!ng_label_mirror)
        ng_label_mirror =
            (void (*)(GLenum, GLuint, GLsizei,
                      const GLchar *))eglGetProcAddress("glObjectLabelKHR");
    }
  }
  if (ng_label_mirror && ng_label_mirror_limit > 0) {
    size_t take = n;
    if (take >= (size_t)ng_label_mirror_limit)
      take = ng_label_mirror_limit - 1;
    ng_label_mirror(ng_label_core_type(type), object, take, text);
  }
}
static int ng_label_dispatch(uint32_t op, const uint8_t *data, size_t size,
                             void *output, size_t capacity, size_t *used) {
  uint64_t a[4] = {0};
  size_t words = op == NG_LABEL_SET ? 3 : 4;
  if (size < words * 8)
    return -1;
  memcpy(a, data, words * 8);
  if (a[2] > 65536 ||
      (op == NG_LABEL_SET ? size != 24 + a[2] : size != 32 || a[3] > 1))
    return -1;
  GLenum error = !ng_label_core_type(a[0])      ? GL_INVALID_ENUM
                 : !ng_label_exists(a[0], a[1]) ? GL_INVALID_OPERATION
                                                : 0;
  if (error)
    ng_error_save(error);
  struct ng_label *entry = ng_labels;
  while (entry && (entry->type != a[0] || entry->object != a[1]))
    entry = entry->next;
  if (op == NG_LABEL_SET) {
    if (error)
      return 0;
    if (!a[2]) {
      ng_label_forget(a[0], a[1]);
      ng_label_native(a[0], a[1], NULL, 0);
      return 0;
    }
    if ((!entry && ng_label_count >= 4096) ||
        a[2] > (4u << 20) - ng_label_bytes + (entry ? entry->length : 0)) {
      struct ng_label **at = &ng_labels;
      while (*at) {
        if (!ng_label_exists((*at)->type, (*at)->object))
          ng_label_drop(at);
        else
          at = &(*at)->next;
      }
      if ((!entry && ng_label_count >= 4096) ||
          a[2] > (4u << 20) - ng_label_bytes + (entry ? entry->length : 0)) {
        ng_error_save(GL_OUT_OF_MEMORY);
        return 0;
      }
    }
    char *text = malloc(a[2] + 1);
    if (!text) {
      ng_error_save(GL_OUT_OF_MEMORY);
      return 0;
    }
    memcpy(text, data + 24, a[2]);
    text[a[2]] = 0;
    if (!entry) {
      entry = calloc(1, sizeof(*entry));
      if (!entry) {
        free(text);
        ng_error_save(GL_OUT_OF_MEMORY);
        return 0;
      }
      entry->type = a[0];
      entry->object = a[1];
      entry->next = ng_labels;
      ng_labels = entry;
      ng_label_count++;
    }
    ng_label_bytes -= entry->length;
    free(entry->text);
    entry->text = text;
    entry->length = a[2];
    ng_label_bytes += a[2];
    ng_label_native(a[0], a[1], text, a[2]);
    return 0;
  }
  size_t bytes = 8 + (a[3] ? a[2] : 0);
  if (*used || bytes > capacity)
    return -1;
  memset(output, 0, bytes);
  uint32_t reply[2] = {0, !!error};
  if (!error && entry) {
    size_t n = entry->length;
    if (a[3]) {
      if (n >= (size_t)a[2])
        n = a[2] ? a[2] - 1 : 0;
      if (n)
        memcpy((uint8_t *)output + 8, entry->text, n);
    }
    reply[0] = n;
  }
  memcpy(output, reply, 8);
  *used = bytes;
  return 0;
}
