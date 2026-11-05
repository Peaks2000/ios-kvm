/* Guest pointers stay here. Each draw snapshots only enabled client arrays into
 * a bounded packet; native private buffers receive those bytes, never pointers.
 */
static void ng_vertex_error(GLenum error) {
  uint64_t value = error;
  ng_enqueue(NG_LOCAL_ERROR, &value, 8);
}
static struct ng_vertex_array *ng_vertex_state(void) {
  struct ios_kvm_native_gl_context *c = ng_current();
  if (!c->max_attributes) {
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &c->max_attributes);
    if (c->max_attributes < 1 || c->max_attributes > NG_ATTRIBUTE_LIMIT)
      ng_failure("vertex attribute limit");
  }
  if (!c->vertex_array_known) {
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &c->vertex_array);
    c->vertex_array_known = 1;
  }
  for (struct ng_vertex_array *v = c->vertex_arrays; v; v = v->next)
    if (v->name == (GLuint)c->vertex_array)
      return v;
  if (c->vertex_array_count == 1024)
    ng_failure("client VAO limit");
  struct ng_vertex_array *v = calloc(1, sizeof(*v));
  if (!v)
    ng_failure("client VAO allocation");
  v->name = c->vertex_array;
  v->element_known = 1;
  v->next = c->vertex_arrays;
  c->vertex_arrays = v;
  c->vertex_array_count++;
  return v;
}
static void ng_vertex_array_bind(GLuint name) {
  struct ios_kvm_native_gl_context *c = ng_current();
  c->vertex_array = 0;
  c->vertex_array_known = !name;
}
static void ng_vertex_buffer_bind(GLenum target, GLuint buffer) {
  struct ios_kvm_native_gl_context *c = ng_current();
  if (target == GL_ARRAY_BUFFER) {
    c->array_buffer = 0;
    c->array_known = !buffer;
  } else if (target == GL_ELEMENT_ARRAY_BUFFER) {
    struct ng_vertex_array *v = ng_vertex_state();
    v->element = 0;
    v->element_known = !buffer;
  }
}
static void ng_vertices_delete(unsigned kind, GLsizei n, const GLuint *names) {
  struct ios_kvm_native_gl_context *c = ng_current();
  if (kind == 1) {
    c->array_known = 0;
    for (struct ng_vertex_array *v = c->vertex_arrays; v; v = v->next)
      v->element_known = 0;
  } else if (kind == 5) {
    c->vertex_array_known = 0;
    for (GLsizei i = 0; i < n; i++) {
      if (!names[i])
        continue;
      struct ng_vertex_array **at = &c->vertex_arrays;
      while (*at && (*at)->name != names[i])
        at = &(*at)->next;
      if (*at) {
        struct ng_vertex_array *v = *at;
        *at = v->next;
        free(v);
        c->vertex_array_count--;
      }
    }
  }
}
static void ng_vertex_enable(GLuint index, unsigned enabled) {
  struct ng_vertex_array *v = ng_vertex_state();
  if (index < (GLuint)ng_current()->max_attributes)
    v->attributes[index].enabled = enabled;
}
static void ng_vertex_divisor(GLuint index, GLuint divisor) {
  struct ng_vertex_array *v = ng_vertex_state();
  if (index < (GLuint)ng_current()->max_attributes)
    v->attributes[index].divisor = divisor;
}
static int ng_vertex_pointer(GLuint index, GLint size, GLenum type,
                             GLboolean normalized, GLsizei stride,
                             const void *pointer) {
  struct ng_vertex_array *v = ng_vertex_state();
  struct ios_kvm_native_gl_context *c = ng_current();
  const char *trace = getenv("IOS_KVM_NATIVE_TRACE_ERRORS");
  if (trace && !strcmp(trace, "1") &&
      (index >= (GLuint)c->max_attributes || !ng_attribute_bytes(size, type) ||
       stride < 0))
    fprintf(stderr,
            "native GLES rejected vertex declaration index=%u size=%d type=%#x "
            "stride=%d\n",
            index, size, type, stride);
  if (index >= (GLuint)c->max_attributes || size < 1 || size > 4 ||
      stride < 0) {
    ng_vertex_error(GL_INVALID_VALUE);
    return 1;
  }
  if (!ng_attribute_bytes(size, type)) {
    ng_vertex_error((type == GL_INT_2_10_10_10_REV ||
                     type == GL_UNSIGNED_INT_2_10_10_10_REV)
                        ? GL_INVALID_OPERATION
                        : GL_INVALID_ENUM);
    return 1;
  }
  if (stride > 2048)
    ng_failure("client vertex stride limit");
  if (!c->array_known) {
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &c->array_buffer);
    c->array_known = 1;
  }
  struct ng_attribute *a = &v->attributes[index];
  a->pointer = pointer;
  a->size = size;
  a->type = type;
  a->normalized = !!normalized;
  a->stride = stride;
  a->client = !c->array_buffer;
  a->defined = 1;
  return a->client;
}
static int ng_vertex_query(GLuint index, GLenum pname, GLint *params) {
  struct ng_vertex_array *v = ng_vertex_state();
  if (index >= (GLuint)ng_current()->max_attributes)
    return 0; /* Let the driver report invalid queries without writing storage.
               */
  struct ng_attribute *a = &v->attributes[index];
  if (pname == GL_VERTEX_ATTRIB_ARRAY_ENABLED) {
    const char *mode = getenv("IOS_KVM_NATIVE_VERTEX_STATE_CACHE");
    if (mode && !strcmp(mode, "0"))
      return 0;
  }
  if (!a->client && pname != GL_VERTEX_ATTRIB_ARRAY_ENABLED)
    return 0;
  GLint value;
  switch (pname) {
  case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
    value = a->enabled;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
    value = 0;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_SIZE:
    value = a->size;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_TYPE:
    value = a->type;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
    value = a->stride;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
    value = a->normalized;
    break;
  default:
    return 0;
  }
  if (!params)
    ng_failure("null vertex query");
  *params = value;
  return 1;
}
void glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer) {
  struct ng_vertex_array *v = ng_vertex_state();
  if (index >= (GLuint)ng_current()->max_attributes) {
    ng_vertex_error(GL_INVALID_VALUE);
    return;
  }
  if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) {
    ng_vertex_error(GL_INVALID_ENUM);
    return;
  }
  if (!pointer)
    ng_failure("null vertex pointer query");
  *pointer = (void *)v->attributes[index].pointer;
}
static int ng_client_attributes(struct ng_vertex_array *v) {
  for (unsigned i = 0; i < NG_ATTRIBUTE_LIMIT; i++)
    if (v->attributes[i].enabled && v->attributes[i].client)
      return 1;
  return 0;
}
static void ng_client_instanced_guard(void) {
  if (ng_client_attributes(ng_vertex_state()))
    ng_failure("client arrays with instanced draws not implemented");
}
static int ng_client_draw(int indexed, GLenum mode, GLint first, GLsizei count,
                          GLenum type, const void *indices) {
  struct ng_vertex_array *v = ng_vertex_state();
  int attributes = ng_client_attributes(v);
  if (indexed && !v->element_known) {
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &v->element);
    v->element_known = 1;
  }
  int client_indices = indexed && !v->element;
  if (!attributes && !client_indices)
    return 0;
  if (count < 0 || first < 0) {
    ng_vertex_error(GL_INVALID_VALUE);
    return 1;
  }
  if ((unsigned)count > NG_VERTEX_LIMIT || (unsigned)first > NG_VERTEX_LIMIT)
    ng_failure("client draw count limit");
  size_t index_size = indexed ? ng_index_bytes(type) : 0;
  if (indexed && !index_size) {
    ng_vertex_error(GL_INVALID_ENUM);
    return 1;
  }
  if (!count)
    return client_indices;
  uint32_t maximum = 0;
  size_t index_bytes = client_indices ? (size_t)count * index_size : 0;
  if (client_indices) {
    if (!indices || ng_index_max(indices, count, index_size,
                                 ng_current()->primitive_restart, &maximum))
      ng_failure("client index range");
  } else if (indexed && attributes) {
    uint64_t args[] = {(uint64_t)count, type, (uintptr_t)indices};
    uint32_t reply[2];
    ng_call(NG_INDEX_MAX, args, sizeof(args), reply, sizeof(reply));
    if (reply[0])
      return 1;
    maximum = reply[1];
  } else {
    if ((uint64_t)first + count > NG_VERTEX_LIMIT)
      ng_failure("client vertex range");
    maximum = first + count - 1;
  }
  size_t bytes = 64 + ((index_bytes + 7) & ~(size_t)7);
  unsigned attribute_count = 0;
  for (unsigned i = 0; i < NG_ATTRIBUTE_LIMIT; i++) {
    struct ng_attribute *a = &v->attributes[i];
    if (!a->enabled || !a->client)
      continue;
    size_t vertices = a->divisor ? 1 : (size_t)maximum + 1;
    size_t n = vertices * ng_attribute_bytes(a->size, a->type);
    size_t padded = (n + 7) & ~(size_t)7;
    if (!a->pointer || padded > NG_LIMIT - 40 || bytes > NG_LIMIT - 40 - padded)
      ng_failure("client vertex packet limit/pointer");
    bytes += 40 + padded;
    attribute_count++;
  }
  uint8_t *packet = calloc(1, bytes);
  if (!packet)
    ng_failure("client draw allocation");
  uint64_t header[] = {
      indexed,         mode,           (uint64_t)first,
      (uint64_t)count, type,           client_indices ? 0 : (uintptr_t)indices,
      index_bytes,     attribute_count};
  memcpy(packet, header, 64);
  if (index_bytes)
    memcpy(packet + 64, indices, index_bytes);
  size_t at = 64 + ((index_bytes + 7) & ~(size_t)7);
  for (unsigned i = 0; i < NG_ATTRIBUTE_LIMIT; i++) {
    struct ng_attribute *a = &v->attributes[i];
    if (!a->enabled || !a->client)
      continue;
    size_t vertex_bytes = ng_attribute_bytes(a->size, a->type);
    size_t vertices = a->divisor ? 1 : (size_t)maximum + 1;
    size_t n = vertices * vertex_bytes;
    uint64_t descriptor[] = {i, (uint64_t)a->size, a->type, a->normalized, n};
    memcpy(packet + at, descriptor, 40);
    at += 40;
    size_t stride = a->stride ? (size_t)a->stride : vertex_bytes;
    if (vertices > 1 &&
        ((vertices - 1) * stride > UINTPTR_MAX - (uintptr_t)a->pointer ||
         vertex_bytes >
             UINTPTR_MAX - (uintptr_t)a->pointer - (vertices - 1) * stride))
      ng_failure("client vertex address overflow");
    for (size_t j = 0; j < vertices; j++)
      memcpy(packet + at + j * vertex_bytes,
             (const uint8_t *)a->pointer + j * stride, vertex_bytes);
    at += (n + 7) & ~(size_t)7;
  }
  ng_enqueue(NG_CLIENT_DRAW, packet, bytes);
  free(packet);
  return 1;
}
