/* Restore a migration snapshot once on a persistent data volume, then hand PID 1 to launchd. */
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int extract(const char *archive, const char *directory)
{
    if (access(archive, R_OK)) return 0;
    pid_t child = fork();
    if (!child) {
        execl("/var/jb/usr/bin/tar", "tar", "-xf", archive, "-C", directory, (char *)0);
        _exit(127);
    }
    int status;
    if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) {
        fprintf(stderr, "ios-kvm: restoring %s failed\n", archive);
        return -1;
    }
    return 0;
}

int main(void)
{
    const char *marker = "/private/var/.ios-kvm-session-seeded-v1";
    struct statfs volume;
    int persistent = !statfs("/private/var", &volume) && !strcmp(volume.f_fstypename, "hfs");
    if (!persistent || access(marker, F_OK)) {
        if (extract("/redwood/share/session/var-firstboot.tar", "/private/var") ||
            extract("/redwood/share/session/jb-current.tar", "/var/jb"))
            return 1;
        if (persistent) {
            const char *temporary = "/private/var/.ios-kvm-session-seeded-v1.new";
            int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd < 0 || write(fd, "v1\n", 3) != 3 || fsync(fd) || close(fd)) return 1;
            if (rename(temporary, marker)) return 1;
            sync();
            puts("ios-kvm: migration restored to the persistent volume; future boots keep its data");
        }
    } else {
        puts("ios-kvm: persistent data already initialized; keeping data, packages and tweaks");
    }
    fflush(NULL);
    execl("/sbin/launchd.darwin", "/sbin/launchd.darwin", (char *)0);
    perror("ios-kvm: launchd");
    return 1;
}
