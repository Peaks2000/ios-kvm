/* ios-kvm native GL research transport. All wire peers currently little-endian.
 */
#ifndef IOS_KVM_NATIVE_GL_PROTOCOL_H
#define IOS_KVM_NATIVE_GL_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "native GL wire encoding requires a little-endian peer"
#endif
#define NG_MAGIC UINT32_C(0x4e474c33)
#define NG_VERSION 7
#define NG_FEATURE_SHARED_READ UINT32_C(1)
struct ng_ack {
  uint32_t magic, features;
};
#define NG_BGRA UINT32_C(0x80e1)
#define NG_LIMIT (16u << 20)
#define NG_TOKEN_BYTES 32
struct ng_hello {
  uint32_t magic, version;
  uint64_t schema;
  uint8_t token[NG_TOKEN_BYTES];
};
struct ng_batch {
  uint32_t magic, sequence, bytes, reply_capacity;
};
struct ng_record {
  uint32_t opcode, bytes;
};
struct ng_reply {
  uint32_t magic, sequence, status, bytes;
};
_Static_assert(sizeof(struct ng_hello) == 48, "hello layout");
_Static_assert(sizeof(struct ng_batch) == 16, "batch layout");
_Static_assert(sizeof(struct ng_record) == 8, "record layout");
/* Manual pointer commands follow the generated scalar range. */
enum {
  NG_GENERATE = 0x10000,
  NG_DELETE,
  NG_SHADER_SOURCE,
  NG_BUFFER_DATA,
  NG_BUFFER_SUB_DATA,
  NG_VERTEX_POINTER,
  NG_DRAW_ELEMENTS,
  NG_GET_INTEGER,
  NG_GET_SHADER,
  NG_GET_PROGRAM,
  NG_GET_LOG,
  NG_LOCATION,
  NG_GET_STRING,
  NG_TEX_IMAGE,
  NG_TEX_SUB_IMAGE,
  NG_READ_PIXELS,
  NG_UNIFORM_ARRAY,
  NG_BIND_ATTRIB_LOCATION,
  NG_FENCE_SYNC,
  NG_DELETE_SYNC,
  NG_CLIENT_WAIT_SYNC,
  NG_WAIT_SYNC,
  NG_IS_SYNC,
  NG_GET_SYNC,
  NG_GET_FLOAT,
  NG_GET_FRAMEBUFFER,
  NG_GET_TEX_LEVEL,
  NG_GET_TEX_PARAMETER,
  NG_GET_VERTEX_ATTRIBUTE,
  NG_GET_ATTACHED,
  NG_GET_ACTIVE_UNIFORM,
  NG_GET_UNIFORM,
  NG_COMPRESSED_IMAGE,
  NG_READ_SHARED,
  NG_CLIENT_DRAW,
  NG_INDEX_MAX,
  NG_LOCAL_ERROR,
  NG_LABEL_SET,
  NG_LABEL_GET
};
/* No process pointers or function addresses are accepted on the wire. */
#endif
