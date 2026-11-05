#include "vertices.h"
static _Thread_local GLuint ng_vertex_buffer;
static _Thread_local GLuint ng_element_buffer;
static void ng_vertices_cleanup(void) {
  glDeleteBuffers(1, &ng_vertex_buffer);
  glDeleteBuffers(1, &ng_element_buffer);
}
static int ng_buffer_index_max(uint64_t count, uint64_t type, uint64_t offset,
                               uint32_t *maximum) {
  size_t index = ng_index_bytes(type);
  if (!index || !count || count > NG_VERTEX_LIMIT || offset > INTPTR_MAX)
    return -1;
  ng_errors_collect();
  GLint binding = 0, mapped = 0;
  GLint64 length = 0;
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &binding);
  if (!binding)
    return -1;
  glGetBufferParameteri64v(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &length);
  glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_MAPPED, &mapped);
  size_t bytes = count * index;
  if (ng_errors_collect() || mapped || length < 0 ||
      offset > (uint64_t)length || bytes > (uint64_t)length - offset)
    return -1;
  void *data =
      glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, offset, bytes, GL_MAP_READ_BIT);
  if (!data) {
    ng_errors_collect();
    return -1;
  }
  int result =
      ng_index_max(data, count, index,
                   glIsEnabled(GL_PRIMITIVE_RESTART_FIXED_INDEX), maximum);
  if (!glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER) || ng_errors_collect())
    result = -1;
  return result;
}
static int ng_vertex_dispatch(uint32_t op, const uint8_t *data, size_t size,
                              void *output, size_t capacity, size_t *used) {
  if (op == NG_LOCAL_ERROR) {
    uint64_t error;
    if (size != 8)
      return -1;
    memcpy(&error, data, 8);
    if (error < 0x500 || error > 0x507)
      return -1;
    ng_error_save(error);
    return 0;
  }
  if (op == NG_INDEX_MAX) {
    uint64_t a[3];
    uint32_t reply[2] = {0};
    if (size != sizeof(a) || capacity < sizeof(reply))
      return -1;
    memcpy(a, data, sizeof(a));
    if (ng_buffer_index_max(a[0], a[1], a[2], &reply[1])) {
      reply[0] = 1;
      ng_error_save(GL_INVALID_OPERATION);
    }
    return ng_output(output, capacity, used, reply, sizeof(reply));
  }
  if (op != NG_CLIENT_DRAW || size < 64)
    return -1;
  uint64_t a[8];
  memcpy(a, data, sizeof(a));
  if (a[0] > 1 || a[2] > NG_VERTEX_LIMIT || !a[3] || a[3] > NG_VERTEX_LIMIT ||
      a[6] > size - 64 || a[7] > NG_ATTRIBUTE_LIMIT || a[5] > INTPTR_MAX)
    return -1;
  size_t index_bytes = ng_index_bytes(a[4]);
  uint32_t maximum = 0;
  GLint old_array = 0, old_element = 0, max_attributes = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_array);
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &old_element);
  glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_attributes);
  if (!a[0]) {
    if (a[4] || a[5] || a[6] || a[2] + a[3] > NG_VERTEX_LIMIT)
      return -1;
    maximum = a[2] + a[3] - 1;
  } else {
    if (a[2] || !index_bytes)
      return -1;
    if (a[6]) {
      if (old_element || a[5] || a[6] != a[3] * index_bytes ||
          ng_index_max(data + 64, a[3], index_bytes,
                       glIsEnabled(GL_PRIMITIVE_RESTART_FIXED_INDEX), &maximum))
        return -1;
    } else if (ng_buffer_index_max(a[3], a[4], a[5], &maximum)) {
      ng_error_save(GL_INVALID_OPERATION);
      return 0;
    }
  }
  struct {
    GLuint index;
    GLint size;
    GLenum type;
    GLboolean normalized;
    size_t bytes;
    const uint8_t *data;
  } attributes[NG_ATTRIBUTE_LIMIT];
  uint64_t seen = 0;
  size_t at = 64 + ((a[6] + 7) & ~(size_t)7);
  if (at > size)
    return -1;
  for (size_t i = 0; i < a[7]; i++) {
    uint64_t d[5];
    if (size - at < sizeof(d))
      return -1;
    memcpy(d, data + at, sizeof(d));
    at += sizeof(d);
    size_t pixel = ng_attribute_bytes(d[1], d[2]);
    if (d[0] >= NG_ATTRIBUTE_LIMIT || d[0] >= (uint64_t)max_attributes ||
        (seen & (UINT64_C(1) << d[0])) || !pixel || d[3] > 1 ||
        d[4] > size - at)
      return -1;
    seen |= UINT64_C(1) << d[0];
    GLint divisor = 0, enabled = 0;
    glGetVertexAttribiv(d[0], GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &divisor);
    glGetVertexAttribiv(d[0], GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
    size_t vertices = divisor ? 1 : (size_t)maximum + 1;
    if (!enabled || d[4] != vertices * pixel)
      return -1;
    attributes[i].index = d[0];
    attributes[i].size = d[1];
    attributes[i].type = d[2];
    attributes[i].normalized = d[3];
    attributes[i].bytes = d[4];
    attributes[i].data = data + at;
    at += (d[4] + 7) & ~(size_t)7;
    if (at > size)
      return -1;
  }
  if (at != size)
    return -1;
  /* All packet ranges are checked before private buffers or draw state change.
   */
  ng_errors_collect();
  if (a[7]) {
    if (!ng_vertex_buffer)
      glGenBuffers(1, &ng_vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, ng_vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, size, data, GL_STREAM_DRAW);
  }
  for (size_t i = 0; i < a[7]; i++) {
    GLuint index = attributes[i].index;
    glVertexAttribPointer(index, attributes[i].size, attributes[i].type,
                          attributes[i].normalized, 0,
                          (const void *)(uintptr_t)(attributes[i].data - data));
  }
  if (a[6]) {
    if (!ng_element_buffer)
      glGenBuffers(1, &ng_element_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ng_element_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, a[6], data + 64, GL_STREAM_DRAW);
  }
  int failed = ng_errors_collect();
  if (!failed) {
    if (a[0])
      glDrawElements(a[1], a[3], a[4], (const void *)(uintptr_t)a[5]);
    else
      glDrawArrays(a[1], a[2], a[3]);
  }
  glBindBuffer(GL_ARRAY_BUFFER, old_array);
  if (a[6])
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, old_element);
  return 0;
}
