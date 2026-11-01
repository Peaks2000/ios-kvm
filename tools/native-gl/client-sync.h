GLsync glFenceSync(GLenum condition, GLbitfield flags) {
  uint64_t a[] = {condition, flags}, token = 0;
  ng_call(NG_FENCE_SYNC, a, sizeof(a), &token, 8);
  return (GLsync)(uintptr_t)token;
}
void glDeleteSync(GLsync sync) {
  uint64_t a[] = {(uintptr_t)sync};
  ng_enqueue(NG_DELETE_SYNC, a, sizeof(a));
}
GLboolean glIsSync(GLsync sync) {
  uint64_t a[] = {(uintptr_t)sync};
  GLboolean value = 0;
  ng_call(NG_IS_SYNC, a, sizeof(a), &value, sizeof(value));
  return value;
}
GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  uint64_t a[] = {(uintptr_t)sync, flags, timeout};
  GLenum value = 0;
  ng_call(NG_CLIENT_WAIT_SYNC, a, sizeof(a), &value, 4);
  return value;
}
void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  uint64_t a[] = {(uintptr_t)sync, flags, timeout};
  ng_enqueue(NG_WAIT_SYNC, a, sizeof(a));
}
void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei *length,
                 GLint *values) {
  if (bufSize < 0 || bufSize > 4096 || (!values && bufSize))
    ng_failure("sync query bounds");
  uint64_t a[] = {(uintptr_t)sync, pname, (uint64_t)bufSize};
  uint8_t output[4 + 4096 * 4];
  size_t n =
      ng_request(NG_GET_SYNC, a, sizeof(a), output, 4 + (size_t)bufSize * 4);
  GLsizei count = 0;
  if (n < 4)
    ng_failure("sync query length");
  memcpy(&count, output, 4);
  if (count < 0 || count > bufSize || n != 4 + (size_t)count * 4)
    ng_failure("sync query payload");
  if (length)
    *length = count;
  if (count)
    memcpy(values, output + 4, count * 4);
}
