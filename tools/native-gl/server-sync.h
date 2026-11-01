/* Per-native-context sync table; wire IDs never contain GL driver pointers. */
#define NG_SYNC_LIMIT 1024
static _Atomic uint64_t ng_sync_serial = 1;
static _Thread_local struct {
  uint64_t token;
  GLsync object;
} ng_syncs[NG_SYNC_LIMIT];
static uint64_t ng_sync_token(void) {
  uint64_t value = atomic_load(&ng_sync_serial);
  do {
    if (!value || value == UINT64_MAX || value > UINTPTR_MAX)
      return 0;
  } while (!atomic_compare_exchange_weak(&ng_sync_serial, &value, value + 1));
  return value;
}
static GLsync ng_sync_find(uint64_t token) {
  for (unsigned i = 0; i < NG_SYNC_LIMIT; i++)
    if (token && ng_syncs[i].token == token)
      return ng_syncs[i].object;
  return NULL;
}
static void ng_sync_cleanup(void) {
  for (unsigned i = 0; i < NG_SYNC_LIMIT; i++)
    if (ng_syncs[i].object) {
      glDeleteSync(ng_syncs[i].object);
      ng_syncs[i].object = NULL;
      ng_syncs[i].token = 0;
    }
}
static int ng_sync_dispatch(uint32_t opcode, const uint8_t *data, size_t size,
                            void *output, size_t capacity, size_t *used) {
  uint64_t a[4] = {0};
  size_t words;
  switch (opcode) {
  case NG_FENCE_SYNC:
    words = 2;
    break;
  case NG_DELETE_SYNC:
  case NG_IS_SYNC:
    words = 1;
    break;
  case NG_CLIENT_WAIT_SYNC:
  case NG_WAIT_SYNC:
  case NG_GET_SYNC:
    words = 3;
    break;
  default:
    return -1;
  }
  if (size != words * 8 || *used)
    return -1;
  memcpy(a, data, size);
  if (opcode == NG_FENCE_SYNC) {
    if (capacity < 8)
      return -1;
    unsigned slot;
    for (slot = 0; slot < NG_SYNC_LIMIT && ng_syncs[slot].object; slot++)
      ;
    if (slot == NG_SYNC_LIMIT)
      return -1;
    GLsync object = glFenceSync(a[0], a[1]);
    uint64_t token = 0;
    if (object) {
      token = ng_sync_token();
      if (!token || token > UINTPTR_MAX) {
        glDeleteSync(object);
        return -1;
      }
      ng_syncs[slot].object = object;
      ng_syncs[slot].token = token;
    }
    return ng_output(output, capacity, used, &token, 8);
  }
  GLsync object = ng_sync_find(a[0]);
  if (opcode == NG_IS_SYNC) {
    GLboolean value = object ? glIsSync(object) : GL_FALSE;
    return ng_output(output, capacity, used, &value, sizeof(value));
  }
  if (opcode == NG_DELETE_SYNC) {
    if (!a[0])
      return 0;
    if (!object)
      return -1;
    for (unsigned i = 0; i < NG_SYNC_LIMIT; i++)
      if (ng_syncs[i].token == a[0]) {
        glDeleteSync(object);
        ng_syncs[i].object = NULL;
        ng_syncs[i].token = 0;
        return 0;
      }
  }
  if (!object)
    return -1;
  if (opcode == NG_CLIENT_WAIT_SYNC) {
    if (capacity < 4)
      return -1;
    GLenum value = glClientWaitSync(object, a[1], a[2]);
    return ng_output(output, capacity, used, &value, 4);
  }
  if (opcode == NG_WAIT_SYNC) {
    glWaitSync(object, a[1], a[2]);
    return 0;
  }
  if (opcode == NG_GET_SYNC) {
    if (a[2] > 4096 || capacity < 4 + a[2] * 4)
      return -1;
    GLsizei length = 0;
    GLint values[4096] = {0};
    /* Each defined sync property has one integer. Query host storage of size1,
     * then copy at most the guest capacity (GLES3.1 section4.1.3). This also
     * avoids a native zero-capacity query returning an unusable length. */
    glGetSynciv(object, a[1], 1, &length, values);
    if (length < 0 || length > 1)
      return -1;
    if ((uint64_t)length > a[2])
      length = (GLsizei)a[2];
    memcpy(output, &length, 4);
    if (length)
      memcpy((uint8_t *)output + 4, values, length * 4);
    *used = 4 + length * 4;
    return 0;
  }
  return -1;
}
