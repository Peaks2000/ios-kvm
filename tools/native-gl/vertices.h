#ifndef IOS_KVM_NATIVE_VERTICES_H
#define IOS_KVM_NATIVE_VERTICES_H
#include "protocol.h"
#define NG_ATTRIBUTE_LIMIT 32
#define NG_VERTEX_LIMIT (1u << 20)
static inline size_t ng_attribute_bytes(uint64_t size, uint64_t type) {
  if (size < 1 || size > 4)
    return 0;
  switch (type) {
  case 0x1400:
  case 0x1401:
    return size; /* byte */
  case 0x1402:
  case 0x1403:
  case 0x140b:
    return size * 2;
  case 0x1404:
  case 0x1405:
  case 0x1406:
  case 0x140c:
    return size * 4;
  case 0x8d9f:
  case 0x8368:
    return size == 4 ? 4 : 0;
  default:
    return 0;
  }
}
static inline size_t ng_index_bytes(uint64_t type) {
  return type == 0x1401 ? 1 : type == 0x1403 ? 2 : type == 0x1405 ? 4 : 0;
}
static inline int ng_index_max(const uint8_t *data, size_t count, size_t size,
                               int restart, uint32_t *maximum) {
  if (!size || count > NG_VERTEX_LIMIT)
    return -1;
  uint32_t limit = size == 1 ? UINT8_MAX : size == 2 ? UINT16_MAX : UINT32_MAX;
  *maximum = 0;
  for (size_t i = 0; i < count; i++) {
    uint32_t index = 0;
    __builtin_memcpy(&index, data + i * size, size);
    if (restart && index == limit)
      continue;
    if (index >= NG_VERTEX_LIMIT)
      return -1;
    if (index > *maximum)
      *maximum = index;
  }
  return 0;
}
#endif
