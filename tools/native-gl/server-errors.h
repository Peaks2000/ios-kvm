/* Preserve GL errors inspected internally while checking a transfer. */
static _Thread_local uint32_t ng_saved_errors;
static _Thread_local GLenum ng_saved_extra_error;
static void ng_error_save(GLenum error) {
  if (error >= 0x500 && error < 0x520)
    ng_saved_errors |= UINT32_C(1) << (error - 0x500);
  else if (error)
    ng_saved_extra_error = error;
}
static int ng_errors_collect(void) {
  int found = 0;
  for (unsigned i = 0; i < 32; i++) {
    GLenum error = glGetError();
    if (!error)
      break;
    ng_error_save(error);
    found = 1;
  }
  return found;
}
static GLenum ng_error_get(void) {
  GLenum error = glGetError();
  if (error) {
    if (error >= 0x500 && error < 0x520)
      ng_saved_errors &= ~(UINT32_C(1) << (error - 0x500));
    else if (error == ng_saved_extra_error)
      ng_saved_extra_error = 0;
    return error;
  }
  for (unsigned bit = 0; bit < 32; bit++)
    if (ng_saved_errors & (UINT32_C(1) << bit)) {
      ng_saved_errors &= ~(UINT32_C(1) << bit);
      return 0x500 + bit;
    }
  error = ng_saved_extra_error;
  ng_saved_extra_error = 0;
  return error;
}
