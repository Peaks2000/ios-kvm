static size_t ng_blob(uint32_t op, const uint64_t *args, size_t count,
                      const void *data, size_t size, void *output,
                      size_t capacity, int query) {
  if (count > 9 || size > NG_LIMIT - count * 8)
    ng_failure("blob limit");
  size_t bytes = count * 8 + size;
  uint8_t *p = malloc(bytes ? bytes : 1);
  if (!p)
    ng_failure("blob allocation");
  memcpy(p, args, count * 8);
  if (size)
    memcpy(p + count * 8, data, size);
  size_t result = 0;
  if (query)
    result = ng_request(op, p, bytes, output, capacity);
  else
    ng_enqueue(op, p, bytes);
  free(p);
  return result;
}
static void ng_generate(unsigned kind, GLsizei n, GLuint *names) {
  if (n < 0 || n > 4096 || (!names && n))
    ng_failure("object count");
  uint64_t a[] = {kind, (uint64_t)n};
  if (ng_blob(NG_GENERATE, a, 2, NULL, 0, names, n * 4, 1) != (size_t)n * 4)
    ng_failure("object reply");
}
static void ng_delete(unsigned kind, GLsizei n, const GLuint *names) {
  if (n < 0 || n > 4096 || (!names && n))
    ng_failure("object count");
  uint64_t a[] = {kind, (uint64_t)n};
  ng_blob(NG_DELETE, a, 2, names, n * 4, NULL, 0, 0);
  if (kind == 1)
    ng_pixel_buffers_forget();
}
#define OBJECT(kind, name)                                                     \
  void glGen##name(GLsizei n, GLuint *p) { ng_generate(kind, n, p); }          \
  void glDelete##name(GLsizei n, const GLuint *p) { ng_delete(kind, n, p); }
OBJECT(1, Buffers)
OBJECT(2, Textures)
OBJECT(3, Framebuffers)
OBJECT(4, Renderbuffers)
OBJECT(5, VertexArrays) OBJECT(6, Samplers) OBJECT(7, Queries)
    OBJECT(8, TransformFeedbacks)
#undef OBJECT
        void glShaderSource(GLuint shader, GLsizei count,
                            const GLchar *const *strings,
                            const GLint *lengths) {
  if (count < 1 || count > 1024 || !strings)
    ng_failure("shader strings");
  size_t bytes = 4;
  for (GLsizei i = 0; i < count; i++) {
    if (!strings[i])
      ng_failure("null shader string");
    size_t n =
        lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
    if (n > NG_LIMIT - bytes - 4)
      ng_failure("shader size");
    bytes += 4 + n;
  }
  uint8_t *blob = malloc(bytes);
  if (!blob)
    ng_failure("shader allocation");
  uint32_t nstrings = count;
  memcpy(blob, &nstrings, 4);
  size_t at = 4;
  for (GLsizei i = 0; i < count; i++) {
    uint32_t n = lengths && lengths[i] >= 0 ? (uint32_t)lengths[i]
                                            : (uint32_t)strlen(strings[i]);
    memcpy(blob + at, &n, 4);
    at += 4;
    memcpy(blob + at, strings[i], n);
    at += n;
  }
  uint64_t a[] = {shader};
  ng_blob(NG_SHADER_SOURCE, a, 1, blob, bytes, NULL, 0, 0);
  free(blob);
}
void glBufferData(GLenum target, GLsizeiptr size, const void *data,
                  GLenum usage) {
  if (size < 0 || (uint64_t)size > NG_LIMIT)
    ng_failure("buffer size");
  uint64_t a[] = {target, size, usage, data != NULL};
  ng_blob(NG_BUFFER_DATA, a, 4, data, data ? (size_t)size : 0, NULL, 0, 0);
}
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                     const void *data) {
  if (offset < 0 || size < 0 || (uint64_t)size > NG_LIMIT || (!data && size))
    ng_failure("buffer subdata");
  uint64_t a[] = {target, offset, size};
  ng_blob(NG_BUFFER_SUB_DATA, a, 3, data, size, NULL, 0, 0);
}
void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                           GLboolean normalized, GLsizei stride,
                           const void *pointer) {
  uint64_t a[] = {index,      (uint64_t)size,   type,
                  normalized, (uint64_t)stride, (uintptr_t)pointer};
  ng_blob(NG_VERTEX_POINTER, a, 6, NULL, 0, NULL, 0, 0);
}
void glDrawElements(GLenum mode, GLsizei count, GLenum type,
                    const void *indices) {
  uint64_t a[] = {mode, (uint64_t)count, type, (uintptr_t)indices};
  ng_blob(NG_DRAW_ELEMENTS, a, 4, NULL, 0, NULL, 0, 0);
}
void glGetIntegerv(GLenum pname, GLint *params) {
  GLint values[4096];
  uint64_t a[] = {pname};
  size_t n = ng_blob(NG_GET_INTEGER, a, 1, NULL, 0, values, sizeof(values), 1);
  if (n % 4 || (!params && n))
    ng_failure("integer query");
  memcpy(params, values, n);
}
void glGetShaderiv(GLuint object, GLenum pname, GLint *params) {
  uint64_t a[] = {object, pname};
  if (ng_blob(NG_GET_SHADER, a, 2, NULL, 0, params, 4, 1) != 4)
    ng_failure("shader query");
}
void glGetProgramiv(GLuint object, GLenum pname, GLint *params) {
  uint64_t a[] = {object, pname};
  if (ng_blob(NG_GET_PROGRAM, a, 2, NULL, 0, params, 4, 1) != 4)
    ng_failure("program query");
}
static void ng_log(unsigned kind, GLuint object, GLsizei size, GLsizei *length,
                   GLchar *text) {
  if (size < 0 || size > 65536 || (!text && size))
    ng_failure("log size");
  uint8_t *reply = calloc(1, (size_t)size + 4);
  if (!reply)
    ng_failure("log allocation");
  uint64_t a[] = {kind, object, size};
  if (ng_blob(NG_GET_LOG, a, 3, NULL, 0, reply, (size_t)size + 4, 1) !=
      (size_t)size + 4)
    ng_failure("log reply");
  if (length)
    memcpy(length, reply, 4);
  if (size)
    memcpy(text, reply + 4, size);
  free(reply);
}
void glGetShaderInfoLog(GLuint shader, GLsizei size, GLsizei *length,
                        GLchar *text) {
  ng_log(0, shader, size, length, text);
}
void glGetProgramInfoLog(GLuint program, GLsizei size, GLsizei *length,
                         GLchar *text) {
  ng_log(1, program, size, length, text);
}
static GLint ng_location(unsigned kind, GLuint program, const GLchar *name) {
  if (!name)
    ng_failure("null name");
  size_t n = strlen(name) + 1;
  if (n > 65536)
    ng_failure("name size");
  uint64_t a[] = {kind, program};
  GLint value;
  if (ng_blob(NG_LOCATION, a, 2, name, n, &value, 4, 1) != 4)
    ng_failure("location reply");
  return value;
}
void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) {
  if (!name)
    ng_failure("null attribute name");
  size_t bytes = strlen(name) + 1;
  if (bytes > 65536)
    ng_failure("attribute name size");
  uint64_t a[] = {program, index};
  ng_blob(NG_BIND_ATTRIB_LOCATION, a, 2, name, bytes, NULL, 0, 0);
}
GLint glGetUniformLocation(GLuint program, const GLchar *name) {
  return ng_location(0, program, name);
}
GLint glGetAttribLocation(GLuint program, const GLchar *name) {
  return ng_location(1, program, name);
}
static const GLubyte *ng_string(GLenum pname, GLuint index, int indexed) {
  struct ios_kvm_native_gl_context *c = ng_current();
  for (unsigned i = 0; i < c->string_count; i++)
    if (c->strings[i].pname == pname && c->strings[i].index == index &&
        c->strings[i].indexed == indexed)
      return (const GLubyte *)c->strings[i].text;
  char buffer[65536];
  uint64_t a[] = {pname, index, (uint64_t)indexed};
  size_t n = ng_blob(NG_GET_STRING, a, 3, NULL, 0, buffer, sizeof(buffer), 1);
  if (!n)
    return NULL;
  if (buffer[n - 1])
    ng_failure("string terminator");
  if (c->string_count == 4096)
    ng_failure("string cache limit");
  if (c->string_count == c->string_capacity) {
    unsigned capacity = c->string_capacity ? c->string_capacity * 2 : 16;
    struct ng_string_entry *entries =
        realloc(c->strings, capacity * sizeof(*entries));
    if (!entries)
      ng_failure("string cache allocation");
    c->strings = entries;
    c->string_capacity = capacity;
  }
  char *text = malloc(n);
  if (!text)
    ng_failure("string allocation");
  memcpy(text, buffer, n);
  c->strings[c->string_count++] =
      (struct ng_string_entry){pname, index, indexed, text};
  return (const GLubyte *)text;
}
const GLubyte *glGetString(GLenum pname) { return ng_string(pname, 0, 0); }
const GLubyte *glGetStringi(GLenum pname, GLuint index) {
  return ng_string(pname, index, 1);
}
/* Transfer only active pixels, preserving the caller's skipped/padded bytes. */
static size_t ng_pixel_layout(int pack, GLsizei width, GLsizei height,
                              GLenum format, GLenum type, size_t *stride,
                              size_t *skip) {
  if (width < 0 || height < 0 || width > 4096 || height > 4096 ||
      (format != GL_RGBA && format != NG_BGRA) || type != GL_UNSIGNED_BYTE)
    ng_failure("pixel format/size not implemented");
  struct ng_pixel_store state = ng_pixel_store_get(pack);
  GLint alignment = state.alignment, row = state.row, rows = state.rows,
        pixels = state.pixels;
  if (state.buffer)
    ng_failure("PBO transfer not implemented");
  if ((alignment != 1 && alignment != 2 && alignment != 4 && alignment != 8) ||
      row < 0 || rows < 0 || pixels < 0 || row > 65536 || rows > 65536 ||
      pixels > 65536)
    ng_failure("pixel layout");
  *stride =
      (((size_t)(row ? row : width) * 4 + alignment - 1) / (size_t)alignment) *
      (size_t)alignment;
  *skip = (size_t)rows * *stride + (size_t)pixels * 4;
  size_t bytes = (size_t)width * height * 4;
  if (bytes > NG_LIMIT)
    ng_failure("pixel byte limit");
  return bytes;
}
static uint8_t *ng_pack(const void *data, GLsizei width, GLsizei height,
                        size_t stride, size_t skip, size_t bytes) {
  uint8_t *p = malloc(bytes ? bytes : 1);
  if (!p)
    ng_failure("pixel allocation");
  if (bytes)
    for (GLsizei y = 0; y < height; y++)
      memcpy(p + (size_t)y * width * 4,
             (const uint8_t *)data + skip + (size_t)y * stride,
             (size_t)width * 4);
  return p;
}
void glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei width,
                  GLsizei height, GLint border, GLenum format, GLenum type,
                  const void *pixels) {
  size_t stride, skip,
      bytes = ng_pixel_layout(0, width, height, format, type, &stride, &skip);
  uint8_t *p =
      pixels ? ng_pack(pixels, width, height, stride, skip, bytes) : NULL;
  uint64_t a[] = {target,
                  (uint64_t)level,
                  (uint64_t)internal,
                  (uint64_t)width,
                  (uint64_t)height,
                  (uint64_t)border,
                  format,
                  type,
                  pixels != NULL};
  ng_blob(NG_TEX_IMAGE, a, 9, p, p ? bytes : 0, NULL, 0, 0);
  free(p);
}
void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const void *pixels) {
  size_t stride, skip,
      bytes = ng_pixel_layout(0, width, height, format, type, &stride, &skip);
  if (!pixels && bytes)
    ng_failure("null upload");
  uint8_t *p = ng_pack(pixels, width, height, stride, skip, bytes);
  uint64_t a[] = {target,          (uint64_t)level,  (uint64_t)x, (uint64_t)y,
                  (uint64_t)width, (uint64_t)height, format,      type};
  ng_blob(NG_TEX_SUB_IMAGE, a, 8, p, bytes, NULL, 0, 0);
  free(p);
}
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void *pixels) {
  if (ng_shared_try_read(x, y, width, height, format, type, pixels))
    return;
  size_t stride, skip,
      bytes = ng_pixel_layout(1, width, height, format, type, &stride, &skip);
  if (!pixels && bytes)
    ng_failure("null readback");
  uint8_t *p = malloc(bytes ? bytes : 1);
  if (!p)
    ng_failure("pixel allocation");
  uint64_t a[] = {(uint64_t)x,      (uint64_t)y, (uint64_t)width,
                  (uint64_t)height, format,      type};
  if (ng_blob(NG_READ_PIXELS, a, 6, NULL, 0, p, bytes, 1) != bytes)
    ng_failure("pixel reply");
  if (bytes)
    for (GLsizei row = 0; row < height; row++)
      memcpy((uint8_t *)pixels + skip + (size_t)row * stride,
             p + (size_t)row * width * 4, (size_t)width * 4);
  free(p);
}
static void ng_uniform(GLint location, GLsizei count, unsigned components,
                       unsigned kind, GLboolean transpose, const void *values) {
  if (count < 0 || count > 4096)
    ng_failure("uniform count");
  size_t bytes = (size_t)count * components * 4 * (kind == 3 ? components : 1);
  if (bytes && !values)
    ng_failure("uniform data");
  uint64_t a[] = {(uint64_t)location, (uint64_t)count, components, kind,
                  transpose};
  ng_blob(NG_UNIFORM_ARRAY, a, 5, values, bytes, NULL, 0, 0);
}
#define UNIFORM(n, suffix, type, kind)                                         \
  void glUniform##n##suffix(GLint location, GLsizei count, const type *v) {    \
    ng_uniform(location, count, n, kind, 0, v);                                \
  }
UNIFORM(1, fv, GLfloat, 0)
UNIFORM(2, fv, GLfloat, 0)
UNIFORM(3, fv, GLfloat, 0)
UNIFORM(4, fv, GLfloat, 0)
UNIFORM(1, iv, GLint, 1) UNIFORM(2, iv, GLint, 1) UNIFORM(3, iv, GLint, 1)
    UNIFORM(4, iv, GLint, 1) UNIFORM(1, uiv, GLuint, 2)
        UNIFORM(2, uiv, GLuint, 2) UNIFORM(3, uiv, GLuint, 2)
            UNIFORM(4, uiv, GLuint, 2)
#undef UNIFORM
#define MATRIX(n)                                                              \
  void glUniformMatrix##n##fv(GLint location, GLsizei count,                   \
                              GLboolean transpose, const GLfloat *v) {         \
    ng_uniform(location, count, n, 3, transpose, v);                           \
  }
                MATRIX(2) MATRIX(3) MATRIX(4)
#undef MATRIX
