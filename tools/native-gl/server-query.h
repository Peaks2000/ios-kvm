/* Native query storage is sized independently from guest output pointers. */
struct ng_uniform_info {
  GLuint program;
  GLint location;
  unsigned count;
};
static _Thread_local struct ng_uniform_info ng_uniform_cache[512];
static _Thread_local unsigned ng_uniform_cursor;
static void ng_uniform_forget(GLuint program) {
  for (unsigned i = 0; i < 512; i++)
    if (ng_uniform_cache[i].program == program)
      ng_uniform_cache[i].count = 0;
}
static unsigned ng_uniform_components(GLenum type) {
  switch (type) {
  case GL_FLOAT_VEC2:
  case GL_INT_VEC2:
  case GL_UNSIGNED_INT_VEC2:
  case GL_BOOL_VEC2:
    return 2;
  case GL_FLOAT_VEC3:
  case GL_INT_VEC3:
  case GL_UNSIGNED_INT_VEC3:
  case GL_BOOL_VEC3:
    return 3;
  case GL_FLOAT_VEC4:
  case GL_INT_VEC4:
  case GL_UNSIGNED_INT_VEC4:
  case GL_BOOL_VEC4:
  case GL_FLOAT_MAT2:
    return 4;
  case GL_FLOAT_MAT3:
    return 9;
  case GL_FLOAT_MAT4:
    return 16;
  case GL_FLOAT_MAT2x3:
  case GL_FLOAT_MAT3x2:
    return 6;
  case GL_FLOAT_MAT2x4:
  case GL_FLOAT_MAT4x2:
    return 8;
  case GL_FLOAT_MAT3x4:
  case GL_FLOAT_MAT4x3:
    return 12;
  default:
    return 1; /* Remaining GLES3 scalar, sampler and image uniform types. */
  }
}
static int ng_uniform_count(GLuint program, GLint location) {
  if (location < 0)
    return -1;
  for (unsigned i = 0; i < 512; i++)
    if (ng_uniform_cache[i].count && ng_uniform_cache[i].program == program &&
        ng_uniform_cache[i].location == location)
      return ng_uniform_cache[i].count;
  GLint uniforms = 0, max_name = 0;
  glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &uniforms);
  glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_name);
  if (uniforms < 0 || uniforms > 4096 || max_name < 1 || max_name > 65536)
    return -1;
  char name[65536], element[65536];
  unsigned attempts = 0;
  for (GLint i = 0; i < uniforms; i++) {
    GLsizei length = 0;
    GLint size = 0;
    GLenum type = 0;
    glGetActiveUniform(program, i, max_name, &length, &size, &type, name);
    if (length < 0 || length >= max_name || size < 1 || size > 4096)
      return -1;
    name[length] = 0;
    int found = glGetUniformLocation(program, name) == location;
    for (char *zero = name; !found && size > 1 && (zero = strstr(zero, "[0]"));
         zero += 3) {
      size_t prefix = (size_t)(zero - name);
      for (GLint j = 1; j < size && !found; j++) {
        if (++attempts > 65536)
          return -1;
        int n = snprintf(element, sizeof(element), "%.*s[%d]%s", (int)prefix,
                         name, j, zero + 3);
        if (n < 0 || (size_t)n >= sizeof(element))
          return -1;
        found = glGetUniformLocation(program, element) == location;
      }
    }
    if (found) {
      unsigned count = ng_uniform_components(type);
      ng_uniform_cache[ng_uniform_cursor++ % 512] =
          (struct ng_uniform_info){program, location, count};
      return count;
    }
  }
  return -1;
}
static int ng_compressed_bytes(GLenum format, uint64_t w, uint64_t h,
                               size_t *bytes) {
  unsigned bw = 4, bh = 4, block = 0;
  if (format >= 0x9270 && format <= 0x9279) {
    unsigned code = format - 0x9270;
    block = (code == 2 || code == 3 || code >= 8) ? 16 : 8;
  } else if (format == 0x8d64)
    block = 8;
  else if ((format >= 0x83f0 && format <= 0x83f3) ||
           (format >= 0x8c4c && format <= 0x8c4f))
    block = (format & 3) < 2 ? 8 : 16;
  else if (format >= 0x8dbb && format <= 0x8dbe)
    block = format <= 0x8dbc ? 8 : 16;
  else if (format >= 0x8e8c && format <= 0x8e8f)
    block = 16;
  else if (format >= 0x8c70 && format <= 0x8c73)
    block = (format & 3) < 2 ? 8 : 16;
  else if (format == 0x86b0 || format == 0x86b1) {
    bw = 8;
    block = 16;
  } else if ((format >= 0x93b0 && format <= 0x93bd) ||
             (format >= 0x93d0 && format <= 0x93dd)) {
    static const unsigned sizes[14][2] = {
        {4, 4}, {5, 4},  {5, 5},  {6, 5},  {6, 6},   {8, 5},   {8, 6},
        {8, 8}, {10, 5}, {10, 6}, {10, 8}, {10, 10}, {12, 10}, {12, 12}};
    unsigned index = format & 15;
    bw = sizes[index][0];
    bh = sizes[index][1];
    block = 16;
  } else if (format >= 0x8c00 && format <= 0x8c03) {
    unsigned bpp = (format & 1) ? 2 : 4;
    uint64_t wide = w > ((format & 1) ? 16 : 8) ? w : ((format & 1) ? 16 : 8),
             high = h > 8 ? h : 8;
    *bytes = (size_t)(wide * high * bpp / 8);
    return 0;
  } else
    return -1;
  *bytes = (size_t)(((w + bw - 1) / bw) * ((h + bh - 1) / bh) * block);
  return 0;
}
static int ng_query(uint32_t opcode, const uint8_t *data, size_t size,
                    void *output, size_t capacity, size_t *used) {
  uint64_t a[8] = {0};
  size_t words;
  switch (opcode) {
  case NG_GET_FLOAT:
    words = 1;
    break;
  case NG_GET_ATTACHED:
  case NG_GET_TEX_PARAMETER:
  case NG_GET_VERTEX_ATTRIBUTE:
    words = 2;
    break;
  case NG_GET_FRAMEBUFFER:
  case NG_GET_TEX_LEVEL:
  case NG_GET_UNIFORM:
    words = 3;
    break;
  case NG_GET_ACTIVE_UNIFORM:
    words = 3;
    break;
  case NG_COMPRESSED_IMAGE:
    words = 8;
    break;
  default:
    return -1;
  }
  if (size < words * 8 || *used)
    return -1;
  memcpy(a, data, words * 8);
  const uint8_t *blob = data + words * 8;
  size_t bytes = size - words * 8;
  if (opcode != NG_COMPRESSED_IMAGE && bytes)
    return -1;
  switch (opcode) {
  case NG_GET_FLOAT: {
    GLfloat values[4096] = {0};
    int n = ng_count(a[0]);
    if (n < 0)
      return -1;
    glGetFloatv(a[0], values);
    return ng_output(output, capacity, used, values, (size_t)n * 4);
  }
  case NG_GET_FRAMEBUFFER: {
    GLint value = 0;
    glGetFramebufferAttachmentParameteriv(a[0], a[1], a[2], &value);
    return ng_output(output, capacity, used, &value, 4);
  }
  case NG_GET_TEX_LEVEL: {
    GLint value = 0;
    glGetTexLevelParameteriv(a[0], (GLint)a[1], a[2], &value);
    return ng_output(output, capacity, used, &value, 4);
  }
  case NG_GET_TEX_PARAMETER: {
    GLint values[4] = {0};
    glGetTexParameteriv(a[0], a[1], values);
    size_t count = (a[1] == 0x1004 || a[1] == 0x8e46) ? 4 : 1;
    return ng_output(output, capacity, used, values, count * 4);
  }
  case NG_GET_VERTEX_ATTRIBUTE: {
    GLint values[4] = {0};
    glGetVertexAttribiv(a[0], a[1], values);
    return ng_output(output, capacity, used, values,
                     (a[1] == GL_CURRENT_VERTEX_ATTRIB ? 4 : 1) * 4);
  }
  case NG_GET_ATTACHED: {
    if (a[1] > 4096 || capacity < 4 + a[1] * 4)
      return -1;
    GLuint shaders[4096] = {0};
    GLsizei count = 0;
    glGetAttachedShaders(a[0], 4096, &count, shaders);
    if (count < 0 || count > 4096)
      return -1;
    if ((uint64_t)count > a[1])
      count = (GLsizei)a[1];
    memcpy(output, &count, 4);
    if (count)
      memcpy((uint8_t *)output + 4, shaders, count * 4);
    *used = 4 + count * 4;
    return 0;
  }
  case NG_GET_ACTIVE_UNIFORM: {
    if (a[2] > 65536 || capacity < 12 + a[2])
      return -1;
    GLsizei length = 0;
    GLint count = 0;
    GLenum type = 0;
    char name[65536] = {0};
    glGetActiveUniform(a[0], a[1], sizeof(name), &length, &count, &type, name);
    if (length < 0 || length >= (GLsizei)sizeof(name))
      return -1;
    GLsizei copied =
        a[2] ? ((uint64_t)length < a[2] ? length : (GLsizei)a[2] - 1) : 0;
    if (a[2]) {
      memcpy((uint8_t *)output + 12, name, copied);
      ((char *)output)[12 + copied] = 0;
    }
    length = copied;
    memcpy(output, &length, 4);
    memcpy((uint8_t *)output + 4, &count, 4);
    memcpy((uint8_t *)output + 8, &type, 4);
    *used = 12 + a[2];
    return 0;
  }
  case NG_GET_UNIFORM: {
    if (a[2] > 1)
      return -1;
    int count = ng_uniform_count(a[0], (GLint)a[1]);
    if (count < 1 || count > 16 || capacity < (size_t)count * 4)
      return -1;
    union {
      GLfloat f[16];
      GLint i[16];
    } values = {0};
    if (a[2])
      glGetUniformiv(a[0], (GLint)a[1], values.i);
    else
      glGetUniformfv(a[0], (GLint)a[1], values.f);
    return ng_output(output, capacity, used, &values, count * 4);
  }
  case NG_COMPRESSED_IMAGE: {
    if (a[3] > 4096 || a[4] > 4096 || a[6] > NG_LIMIT || a[7] > 1 ||
        bytes != (a[7] ? a[6] : 0))
      return -1;
    size_t expected = 0;
    if (ng_compressed_bytes(a[2], a[3], a[4], &expected) || expected != a[6])
      return -1;
    GLint unpack = 0;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
    if (unpack)
      return -1;
    glCompressedTexImage2D(a[0], (GLint)a[1], a[2], a[3], a[4], (GLint)a[5],
                           a[6], a[7] ? blob : NULL);
    return 0;
  }
  default:
    return -1;
  }
}
