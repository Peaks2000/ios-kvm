/* All mutations of this private native context pass through the proxy. Track
 * valid pixel-store writes locally; invalid writes still reach the driver for
 * their GL error and leave the shadow unchanged. PBO binds are queried lazily
 * because the driver may reject a buffer name. */
static void ng_pixel_store_update(GLenum pname, GLint param) {
  struct ios_kvm_native_gl_context *c = ng_current();
  for (unsigned pack = 0; pack < 2; pack++) {
    struct ng_pixel_store *s = &c->pixel_store[pack];
    if (pname == (pack ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT)) {
      if (param == 1 || param == 2 || param == 4 || param == 8)
        s->alignment = param;
    } else if (param >= 0) {
      if (pname == (pack ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH))
        s->row = param;
      else if (pname == (pack ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS))
        s->rows = param;
      else if (pname == (pack ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS))
        s->pixels = param;
    }
  }
}
static void ng_pixel_buffer_bind(GLenum target) {
  struct ios_kvm_native_gl_context *c = ng_current();
  if (target == GL_PIXEL_PACK_BUFFER)
    c->pixel_store[1].buffer_known = 0;
  else if (target == GL_PIXEL_UNPACK_BUFFER)
    c->pixel_store[0].buffer_known = 0;
}
static void ng_pixel_buffers_forget(void) {
  struct ios_kvm_native_gl_context *c = ng_current();
  c->pixel_store[0].buffer_known = c->pixel_store[1].buffer_known = 0;
}
static struct ng_pixel_store ng_pixel_store_get(int pack) {
  struct ng_pixel_store *s = &ng_current()->pixel_store[pack];
  const char *mode = getenv("IOS_KVM_NATIVE_STATE_CACHE");
  if (mode && !strcmp(mode, "0")) {
    struct ng_pixel_store actual = {0};
    glGetIntegerv(pack ? GL_PACK_ALIGNMENT : GL_UNPACK_ALIGNMENT,
                  &actual.alignment);
    glGetIntegerv(pack ? GL_PACK_ROW_LENGTH : GL_UNPACK_ROW_LENGTH,
                  &actual.row);
    glGetIntegerv(pack ? GL_PACK_SKIP_ROWS : GL_UNPACK_SKIP_ROWS, &actual.rows);
    glGetIntegerv(pack ? GL_PACK_SKIP_PIXELS : GL_UNPACK_SKIP_PIXELS,
                  &actual.pixels);
    glGetIntegerv(pack ? GL_PIXEL_PACK_BUFFER_BINDING
                       : GL_PIXEL_UNPACK_BUFFER_BINDING,
                  &actual.buffer);
    return actual;
  }
  if (!s->buffer_known) {
    glGetIntegerv(pack ? GL_PIXEL_PACK_BUFFER_BINDING
                       : GL_PIXEL_UNPACK_BUFFER_BINDING,
                  &s->buffer);
    s->buffer_known = 1;
  }
  return *s;
}
