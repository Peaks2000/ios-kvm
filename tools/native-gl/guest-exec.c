/* Launch only a temporary diagnostic with the compositor's mobile identity. */
#define _DARWIN_C_SOURCE 1
#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
  if (argc < 3 || strcmp(argv[1], "501"))
    return 2;
  gid_t group = 501;
  if (setgroups(1, &group) || setgid(group) || setuid(501)) {
    perror("native diagnostic identity");
    return 1;
  }
  if (getuid() != 501 || geteuid() != 501 || getgid() != 501 || getegid() != 501)
    return 1;
  fprintf(stderr, "Native guest identity: uid=501 gid=501\n");
  fflush(stderr);
  execv(argv[2], argv + 2);
  perror("native diagnostic exec");
  return 1;
}
