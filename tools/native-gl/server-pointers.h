/* Explicit pointer marshalling. Host pointers are allocated here, never
 * received. */
static int ng_count(GLenum pname) {
  switch (pname) {
  case GL_VIEWPORT:
  case GL_SCISSOR_BOX:
  case GL_COLOR_CLEAR_VALUE:
  case GL_COLOR_WRITEMASK:
  case GL_BLEND_COLOR:
    return 4;
  case GL_DEPTH_RANGE:
  case GL_ALIASED_LINE_WIDTH_RANGE:
  case GL_ALIASED_POINT_SIZE_RANGE:
  case GL_MAX_VIEWPORT_DIMS:
    return 2;
  case GL_COMPRESSED_TEXTURE_FORMATS: {
    GLint n = 0;
    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &n);
    return n >= 0 && n <= 4096 ? n : -1;
  }
  case GL_SHADER_BINARY_FORMATS: {
    GLint n = 0;
    glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &n);
    return n >= 0 && n <= 4096 ? n : -1;
  }
  case GL_PROGRAM_BINARY_FORMATS: {
    GLint n = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &n);
    return n >= 0 && n <= 4096 ? n : -1;
  }
  default:
    return 1;
  }
}
static int ng_objects(int generate, uint64_t kind, GLsizei n, GLuint *values) {
  if (n < 0 || n > 4096)
    return -1;
  if (!generate)
    ng_labels_objects(0,kind,n,values);
#define OBJECT(k, name)                                                        \
  case k:                                                                      \
    if (generate)                                                              \
      glGen##name(n, values);                                                  \
    else                                                                       \
      glDelete##name(n, values);                                               \
    break
  switch (kind) {
    OBJECT(1, Buffers);
    OBJECT(2, Textures);
    OBJECT(3, Framebuffers);
    OBJECT(4, Renderbuffers);
    OBJECT(5, VertexArrays);
    OBJECT(6, Samplers);
    OBJECT(7, Queries);
    OBJECT(8, TransformFeedbacks);
  default:
    return -1;
  }
#undef OBJECT
  if (generate)
    ng_labels_objects(1,kind,n,values);
  return 0;
}
static int ng_pixels(uint64_t width, uint64_t height, uint64_t format,
                     uint64_t type, size_t *bytes) {
  size_t pixel = ng_pixel_size(format, type);
  if (width > 4096 || height > 4096 || !pixel)
    return -1;
  *bytes = (size_t)width * height * pixel;
  return *bytes <= NG_LIMIT ? 0 : -1;
}
static void ng_swap_rb(uint8_t *to, const uint8_t *from, size_t n) {
  for (size_t i = 0; i < n; i += 4) {
    uint8_t r = from[i], b = from[i + 2];
    to[i] = b;
    to[i + 1] = from[i + 1];
    to[i + 2] = r;
    to[i + 3] = from[i + 3];
  }
}
static int ng_pixel_state(int packing, GLint old[4]) {
  GLenum names[] = {packing ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT,
                    packing ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH,
                    packing ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS,
                    packing ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS};
  GLint buffer;
  glGetIntegerv(packing ? GL_PIXEL_PACK_BUFFER_BINDING
                        : GL_PIXEL_UNPACK_BUFFER_BINDING,
                &buffer);
  if (buffer)
    return -1;
  for (unsigned i = 0; i < 4; i++) {
    glGetIntegerv(names[i], old + i);
    glPixelStorei(names[i], i ? 0 : 1);
  }
  return 0;
}
static void ng_pixel_restore(int packing, const GLint old[4]) {
  GLenum names[] = {packing ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT,
                    packing ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH,
                    packing ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS,
                    packing ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS};
  for (unsigned i = 0; i < 4; i++)
    glPixelStorei(names[i], old[i]);
}
static int ng_pointer(uint32_t opcode, const uint8_t *data, size_t size,
                      void *output, size_t capacity, size_t *used) {
  size_t words;
  switch (opcode) {
  case NG_GENERATE:
  case NG_DELETE:
  case NG_GET_SHADER:
  case NG_GET_PROGRAM:
  case NG_LOCATION:
  case NG_BIND_ATTRIB_LOCATION:
    words = 2;
    break;
  case NG_SHADER_SOURCE:
  case NG_GET_INTEGER:
    words = 1;
    break;
  case NG_BUFFER_DATA:
  case NG_DRAW_ELEMENTS:
    words = 4;
    break;
  case NG_BUFFER_SUB_DATA:
  case NG_GET_LOG:
  case NG_GET_STRING:
    words = 3;
    break;
  case NG_VERTEX_POINTER:
  case NG_READ_PIXELS:
    words = 6;
    break;
  case NG_TEX_IMAGE:
    words = 9;
    break;
  case NG_TEX_SUB_IMAGE:
    words = 8;
    break;
  case NG_UNIFORM_ARRAY:
    words = 5;
    break;
  default:
    return -1;
  }
  if (size < words * 8 || *used)
    return -1;
  uint64_t a[9] = {0};
  memcpy(a, data, words * 8);
  const uint8_t *blob = data + words * 8;
  size_t bytes = size - words * 8;
  switch (opcode) {
  case NG_GENERATE: {
    if (a[1] > 4096 || bytes || a[1] * 4 > capacity)
      return -1;
    size_t n = a[1] * 4;
    if (ng_objects(1, a[0], a[1], output))
      return -1;
    *used = n;
    return 0;
  }
  case NG_DELETE:
    if (a[1] > 4096 || bytes != a[1] * 4)
      return -1;
    return ng_objects(0, a[0], a[1], (GLuint *)(uintptr_t)blob);
  case NG_SHADER_SOURCE: {
    if (bytes < 4)
      return -1;
    uint32_t count;
    memcpy(&count, blob, 4);
    if (!count || count > 1024)
      return -1;
    size_t at = 4;
    const GLchar **strings = calloc(count, sizeof(*strings));
    GLint *lengths = calloc(count, sizeof(*lengths));
    if (!strings || !lengths) {
      free(strings);
      free(lengths);
      return -1;
    }
    int result = -1;
    for (unsigned i = 0; i < count; i++) {
      if (bytes - at < 4)
        goto source_done;
      uint32_t n;
      memcpy(&n, blob + at, 4);
      at += 4;
      if (n > bytes - at || n > INT32_MAX)
        goto source_done;
      strings[i] = (const GLchar *)blob + at;
      lengths[i] = n;
      at += n;
    }
    if (at != bytes)
      goto source_done;
    ng_trace_shader(a[0], count, strings, lengths);
    glShaderSource(a[0], count, strings, lengths);
    result = 0;
  source_done:
    free(strings);
    free(lengths);
    return result;
  }
  case NG_BUFFER_DATA:
    if (a[1] > NG_LIMIT || a[3] > 1 || bytes != (a[3] ? a[1] : 0))
      return -1;
    glBufferData(a[0], a[1], a[3] ? blob : NULL, a[2]);
    return 0;
  case NG_BUFFER_SUB_DATA:
    if (a[1] > INT64_MAX || a[2] > NG_LIMIT || bytes != a[2])
      return -1;
    glBufferSubData(a[0], a[1], a[2], blob);
    return 0;
  case NG_VERTEX_POINTER: {
    GLint buffer = 0;
    if (bytes || a[5] > INTPTR_MAX)
      return -1;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
    if (!buffer)
      return -1;
    glVertexAttribPointer(a[0], (GLint)a[1], a[2], a[3], (GLsizei)a[4],
                          (const void *)(uintptr_t)a[5]);
    return 0;
  }
  case NG_DRAW_ELEMENTS: {
    GLint buffer = 0;
    if (bytes || a[3] > INTPTR_MAX)
      return -1;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &buffer);
    if (!buffer)
      return -1;
    ng_trace_draw("glDrawElements", a[0], 0, (GLsizei)a[1]);
    glDrawElements(a[0], (GLsizei)a[1], a[2], (const void *)(uintptr_t)a[3]);
    return 0;
  }
  case NG_GET_INTEGER: {
    if (bytes)
      return -1;
    GLint values[4096] = {0};
    int n = ng_count(a[0]);
    if (n < 0)
      return -1;
    glGetIntegerv(a[0], values);
    return ng_output(output, capacity, used, values, (size_t)n * 4);
  }
  case NG_GET_SHADER:
  case NG_GET_PROGRAM: {
    if (bytes)
      return -1;
    GLint value = 0;
    if (opcode == NG_GET_SHADER)
      glGetShaderiv(a[0], a[1], &value);
    else
      glGetProgramiv(a[0], a[1], &value);
    return ng_output(output, capacity, used, &value, 4);
  }
  case NG_GET_LOG: {
    if (bytes || a[0] > 1 || a[2] > 65536 || a[2] + 4 > capacity)
      return -1;
    GLsizei length = 0;
    char *text = (char *)output + 4;
    if (a[0])
      glGetProgramInfoLog(a[1], a[2], &length, text);
    else
      glGetShaderInfoLog(a[1], a[2], &length, text);
    memcpy(output, &length, 4);
    *used = a[2] + 4;
    return 0;
  }
  case NG_BIND_ATTRIB_LOCATION: {
    if (!bytes || bytes > 65536 || blob[bytes - 1])
      return -1;
    glBindAttribLocation(a[0], a[1], (const char *)blob);
    return 0;
  }
  case NG_LOCATION: {
    if (a[0] > 1 || !bytes || bytes > 65536 || blob[bytes - 1])
      return -1;
    GLint value = a[0] ? glGetAttribLocation(a[1], (const char *)blob)
                       : glGetUniformLocation(a[1], (const char *)blob);
    return ng_output(output, capacity, used, &value, 4);
  }
  case NG_GET_STRING: {
    if (bytes || a[2] > 1)
      return -1;
    const GLubyte *s = a[2] ? glGetStringi(a[0], a[1]) : glGetString(a[0]);
    size_t n = s ? strlen((const char *)s) + 1 : 0;
    return ng_output(output, capacity, used, s, n);
  }
  case NG_TEX_IMAGE:
  case NG_TEX_SUB_IMAGE: {
    size_t n;
    int sub = opcode == NG_TEX_SUB_IMAGE;
    unsigned wi = sub ? 4 : 3, hi = sub ? 5 : 4, fi = sub ? 6 : 6,
             ti = sub ? 7 : 7;
    if (ng_pixels(a[wi], a[hi], a[fi], a[ti], &n))
      return -1;
    if (!sub && a[8] > 1)
      return -1;
    if (bytes != ((sub || a[8]) ? n : 0))
      return -1;
    GLint old[4];
    if (ng_pixel_state(0, old))
      return -1;
    uint8_t *converted = NULL;
    const void *pixels = blob;
    if (a[fi] == NG_BGRA && bytes) {
      converted = malloc(bytes);
      if (!converted) {
        ng_pixel_restore(0, old);
        return -1;
      }
      ng_swap_rb(converted, blob, bytes);
      pixels = converted;
    }
    if (sub)
      glTexSubImage2D(a[0], (GLint)a[1], (GLint)a[2], (GLint)a[3], a[4], a[5],
                      a[fi] == NG_BGRA ? GL_RGBA : a[fi],
                      ng_pixel_native_type(a[7]), pixels);
    else
      glTexImage2D(a[0], (GLint)a[1], a[2] == NG_BGRA ? GL_RGBA : (GLint)a[2],
                   a[3], a[4], (GLint)a[5], a[fi] == NG_BGRA ? GL_RGBA : a[fi],
                   ng_pixel_native_type(a[7]), a[8] ? pixels : NULL);
    free(converted);
    ng_pixel_restore(0, old);
    return 0;
  }
  case NG_READ_PIXELS: {
    size_t n;
    if (bytes || ng_pixels(a[2], a[3], a[4], a[5], &n) || capacity < 4 ||
        n > capacity - 4)
      return -1;
    GLint old[4];
    if (ng_pixel_state(1, old))
      return -1;
    ng_errors_collect();
    uint8_t *pixels = (uint8_t *)output + 4;
    ng_trace_draw("copied-readback", 0, 0, a[2] * a[3]);
    glReadPixels((GLint)a[0], (GLint)a[1], a[2], a[3],
                 a[4] == NG_BGRA ? GL_RGBA : a[4], ng_pixel_native_type(a[5]),
                 pixels);
    int32_t failed = ng_errors_collect();
    if (!failed && (a[4] == GL_RGBA || a[4] == NG_BGRA) &&
        a[5] == GL_UNSIGNED_BYTE)
      ng_trace_pixels(a[2], a[3], pixels, 0, 0);
    if (!failed && a[4] == NG_BGRA)
      ng_swap_rb(pixels, pixels, n);
    ng_pixel_restore(1, old);
    if (ng_errors_collect())
      failed = 1;
    memcpy(output, &failed, 4);
    *used = n + 4;
    return 0;
  }
  case NG_UNIFORM_ARRAY: {
    if (a[1] > 4096 || !a[2] || a[2] > 4 || a[3] > 3 || a[4] > 1)
      return -1;
    size_t n = a[1] * a[2] * 4;
    if (a[3] == 3)
      n *= a[2];
    if (bytes != n)
      return -1;
    GLint location = (GLint)a[0];
    GLsizei count = a[1];
#define UNIFORM(kind, suffix, type)                                            \
  if (a[3] == kind) {                                                          \
    const type *v = (const type *)blob;                                        \
    switch (a[2]) {                                                            \
    case 1:                                                                    \
      glUniform1##suffix(location, count, v);                                  \
      break;                                                                   \
    case 2:                                                                    \
      glUniform2##suffix(location, count, v);                                  \
      break;                                                                   \
    case 3:                                                                    \
      glUniform3##suffix(location, count, v);                                  \
      break;                                                                   \
    case 4:                                                                    \
      glUniform4##suffix(location, count, v);                                  \
      break;                                                                   \
    }                                                                          \
    return 0;                                                                  \
  }
    UNIFORM(0, fv, GLfloat);
    UNIFORM(1, iv, GLint);
    UNIFORM(2, uiv, GLuint);
#undef UNIFORM
    const GLfloat *v = (const GLfloat *)blob;
    switch (a[2]) {
    case 2:
      glUniformMatrix2fv(location, count, a[4], v);
      break;
    case 3:
      glUniformMatrix3fv(location, count, a[4], v);
      break;
    case 4:
      glUniformMatrix4fv(location, count, a[4], v);
      break;
    default:
      return -1;
    }
    return 0;
  }
  default:
    return -1;
  }
}
