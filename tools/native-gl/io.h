#ifndef IOS_KVM_NATIVE_GL_IO_H
#define IOS_KVM_NATIVE_GL_IO_H
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
static int ng_io(int fd, void *buffer, size_t bytes, int writing) {
  unsigned char *p = buffer;
  while (bytes) {
#ifdef MSG_NOSIGNAL
    ssize_t n =
        writing ? send(fd, p, bytes, MSG_NOSIGNAL) : recv(fd, p, bytes, 0);
#else
    ssize_t n = writing ? send(fd, p, bytes, 0) : recv(fd, p, bytes, 0);
#endif
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return -1;
    p += n;
    bytes -= (size_t)n;
  }
  return 0;
}
static int ng_token(const char *path, uint8_t token[NG_TOKEN_BYTES]) {
  if (!path)
    return -1;
  int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    return -1;
  struct stat info;
  int result = -1;
  if (!fstat(fd, &info) && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
      !(info.st_mode & 077) && info.st_size == NG_TOKEN_BYTES) {
    size_t at = 0;
    while (at < NG_TOKEN_BYTES) {
      ssize_t n = read(fd, token + at, NG_TOKEN_BYTES - at);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        break;
      at += (size_t)n;
    }
    result = at == NG_TOKEN_BYTES ? 0 : -1;
  }
  close(fd);
  return result;
}
static void ng_socket_options(int fd) {
  int on = 1;
  struct timeval timeout = {20, 0};
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
}
#endif
