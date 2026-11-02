void glGetFloatv(GLenum pname, GLfloat *params) {
  GLfloat values[4096];
  uint64_t a[] = {pname};
  size_t n = ng_blob(NG_GET_FLOAT, a, 1, NULL, 0, values, sizeof(values), 1);
  if (n % 4 || (!params && n))
    ng_failure("float query");
  if (n)
    memcpy(params, values, n);
}
static void ng_integer_query(uint32_t op, const uint64_t *a, size_t count,
                             GLint *params) {
  GLint values[4];
  size_t n = ng_blob(op, a, count, NULL, 0, values, sizeof(values), 1);
  if (n % 4 || (!params && n))
    ng_failure("integer state query");
  if (n)
    memcpy(params, values, n);
}
void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                           GLenum pname, GLint *params) {
  uint64_t a[] = {target, attachment, pname};
  ng_integer_query(NG_GET_FRAMEBUFFER, a, 3, params);
}
void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname,
                              GLint *params) {
  uint64_t a[] = {target, (uint64_t)level, pname};
  ng_integer_query(NG_GET_TEX_LEVEL, a, 3, params);
}
void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
  uint64_t a[] = {target, pname};
  ng_integer_query(NG_GET_TEX_PARAMETER, a, 2, params);
}
void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) {
  uint64_t a[] = {index, pname};
  ng_integer_query(NG_GET_VERTEX_ATTRIBUTE, a, 2, params);
}
void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count,
                          GLuint *shaders) {
  if (maxCount < 0 || maxCount > 4096 || (!shaders && maxCount))
    ng_failure("attached shader capacity");
  uint8_t output[4 + 4096 * 4];
  uint64_t a[] = {program, (uint64_t)maxCount};
  size_t n = ng_blob(NG_GET_ATTACHED, a, 2, NULL, 0, output,
                     4 + (size_t)maxCount * 4, 1);
  GLsizei used = 0;
  if (n < 4)
    ng_failure("attached shader reply");
  memcpy(&used, output, 4);
  if (used < 0 || used > maxCount || n != 4 + (size_t)used * 4)
    ng_failure("attached shader count");
  if (count)
    *count = used;
  if (used)
    memcpy(shaders, output + 4, used * 4);
}
void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                        GLsizei *length, GLint *size, GLenum *type,
                        GLchar *name) {
  if (bufSize < 0 || bufSize > 65536 || (!name && bufSize))
    ng_failure("active uniform capacity");
  uint64_t a[] = {program, index, (uint64_t)bufSize};
  uint8_t *output = malloc(12 + (size_t)bufSize);
  if (!output)
    ng_failure("active uniform allocation");
  size_t n = ng_blob(NG_GET_ACTIVE_UNIFORM, a, 3, NULL, 0, output,
                     12 + (size_t)bufSize, 1);
  if (n != 12 + (size_t)bufSize)
    ng_failure("active uniform reply");
  if (length)
    memcpy(length, output, 4);
  if (size)
    memcpy(size, output + 4, 4);
  if (type)
    memcpy(type, output + 8, 4);
  GLsizei copied = 0;
  memcpy(&copied, output, 4);
  if (copied < 0 || (bufSize && copied >= bufSize) || (!bufSize && copied))
    ng_failure("active uniform name length");
  if (bufSize)
    memcpy(name, output + 12, (size_t)copied + 1);
  free(output);
}
static void ng_get_uniform(GLuint program, GLint location, void *params,
                           int integral) {
  uint64_t a[] = {program, (uint64_t)location, (uint64_t)integral};
  uint8_t values[16 * 4];
  size_t n = ng_blob(NG_GET_UNIFORM, a, 3, NULL, 0, values, sizeof(values), 1);
  if (!n || n % 4 || !params)
    ng_failure("uniform query result");
  memcpy(params, values, n);
}
void glGetUniformfv(GLuint program, GLint location, GLfloat *params) {
  ng_get_uniform(program, location, params, 0);
}
void glGetUniformiv(GLuint program, GLint location, GLint *params) {
  ng_get_uniform(program, location, params, 1);
}
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei imageSize, const void *data) {
  if (width < 0 || height < 0 || width > 4096 || height > 4096 ||
      imageSize < 0 || (uint64_t)imageSize > NG_LIMIT)
    ng_failure("compressed texture bounds");
  GLint unpack = 0;
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
  if (unpack)
    ng_failure("compressed PBO transfer not implemented");
  uint64_t a[] = {
      target,           (uint64_t)level,  internalformat,      (uint64_t)width,
      (uint64_t)height, (uint64_t)border, (uint64_t)imageSize, data != NULL};
  ng_blob(NG_COMPRESSED_IMAGE, a, 8, data, data ? (size_t)imageSize : 0, NULL,
          0, 0);
}
