/* Transfer sizes shared by both peers. Format/type validation by the GL driver
 * remains separate: knowing a byte count does not make every pairing legal. */
#ifndef IOS_KVM_NATIVE_PIXEL_FORMAT_H
#define IOS_KVM_NATIVE_PIXEL_FORMAT_H
#include "protocol.h"
static inline size_t ng_pixel_size(uint64_t format, uint64_t type) {
  unsigned channels;
  switch (format) {
  case 0x1906: /* ALPHA */
  case 0x1909: /* LUMINANCE */
  case 0x1903: /* RED */
    channels = 1;
    break;
  case 0x190a: /* LUMINANCE_ALPHA */
  case 0x8227: /* RG */
    channels = 2;
    break;
  case 0x1907: /* RGB */
    channels = 3;
    break;
  case 0x1908: /* RGBA */
    channels = 4;
    break;
  case NG_BGRA:
    return type == 0x1401 ? 4 : 0;
  default:
    return 0;
  }
  switch (type) {
  case 0x1401: /* UNSIGNED_BYTE */
    return channels;
  case 0x140b: /* HALF_FLOAT */
  case 0x8d61: /* HALF_FLOAT_OES, translated to the GLES3 token on the host */
    return channels * 2;
  case 0x1406: /* FLOAT */
    return channels * 4;
  case 0x8363: /* UNSIGNED_SHORT_5_6_5 */
    return format == 0x1907 ? 2 : 0;
  case 0x8033: /* UNSIGNED_SHORT_4_4_4_4 */
  case 0x8034: /* UNSIGNED_SHORT_5_5_5_1 */
    return format == 0x1908 ? 2 : 0;
  case 0x8368: /* UNSIGNED_INT_2_10_10_10_REV */
    return format == 0x1908 ? 4 : 0;
  default:
    return 0;
  }
}
static inline uint64_t ng_pixel_native_type(uint64_t type) {
  return type == 0x8d61 ? 0x140b : type;
}
#endif
