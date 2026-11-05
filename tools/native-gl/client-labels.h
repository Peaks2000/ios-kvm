#include "labels.h"
void glLabelObjectEXT(GLenum type, GLuint object, GLsizei length,
                      const GLchar *label) {
  if (length < 0) {
    ng_vertex_error(GL_INVALID_VALUE);
    return;
  }
  size_t bytes = !label ? 0 : length ? (size_t)length : strnlen(label, 65537);
  if (bytes > 65536)
    ng_failure("debug label limit");
  uint64_t a[] = {type, object, bytes};
  ng_blob(NG_LABEL_SET, a, 3, label, bytes, NULL, 0, 0);
}
void glGetObjectLabelEXT(GLenum type, GLuint object, GLsizei capacity,
                         GLsizei *length, GLchar *label) {
  if (capacity < 0) {
    ng_vertex_error(GL_INVALID_VALUE);
    return;
  }
  if (capacity > 65536)
    ng_failure("debug label query limit");
  size_t bytes = 8 + (label ? (size_t)capacity : 0);
  uint8_t *reply = calloc(1, bytes);
  if (!reply)
    ng_failure("debug label reply allocation");
  uint64_t a[] = {type, object, (uint64_t)capacity, label != NULL};
  ng_call(NG_LABEL_GET, a, sizeof(a), reply, bytes);
  uint32_t header[2];
  memcpy(header, reply, 8);
  if (!header[1]) {
    if (header[0] > 65536 ||
        (label && capacity && header[0] >= (uint32_t)capacity))
      ng_failure("debug label reply bounds");
    if (length)
      *length = header[0];
    if (label && capacity)
      memcpy(label, reply + 8, (size_t)header[0] + 1);
  }
  free(reply);
}
