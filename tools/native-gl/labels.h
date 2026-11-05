#ifndef IOS_KVM_NATIVE_LABELS_H
#define IOS_KVM_NATIVE_LABELS_H
void glLabelObjectEXT(GLenum type, GLuint object, GLsizei length,
                      const GLchar *label);
void glGetObjectLabelEXT(GLenum type, GLuint object, GLsizei bufSize,
                         GLsizei *length, GLchar *label);
#endif
