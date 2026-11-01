#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <sys/wait.h>
static char control_path[PATH_MAX];
#define IOS_KVM_ASYNC_CONTROL_PATH control_path
#include "gpu-async-policy.h"
static void check(const char *mode,int expected,int live,int fd)
{
    pid_t child=fork();assert(child>=0);
    if(!child){
        if(mode)setenv("IOS_KVM_ASYNC_READBACK",mode,1);else unsetenv("IOS_KVM_ASYNC_READBACK");
        assert(ios_kvm_async_requested()==expected);
        if(live){uint32_t word=1;assert(pwrite(fd,&word,4,0)==4);assert(ios_kvm_async_requested());word=2;assert(pwrite(fd,&word,4,0)==4);assert(!ios_kvm_async_requested());}
        _exit(0);
    }
    int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
}
int main(void)
{
    snprintf(control_path,sizeof(control_path),"/tmp/ios-kvm-async-policy-XXXXXX");int fd=mkstemp(control_path);assert(fd>=0 && !fchmod(fd,0600));
    uint32_t word=0;assert(write(fd,&word,4)==4);
    check(NULL,0,0,fd);check("0",0,0,fd);check("auto",0,0,fd);check("",0,0,fd);check("1",1,0,fd);
    check("marker",0,1,fd);word=1;assert(pwrite(fd,&word,4,0)==4);check("marker",1,0,fd);
    assert(!fchmod(fd,0644));check("marker",0,0,fd);assert(!fchmod(fd,0600));
    assert(!ftruncate(fd,8));check("marker",0,0,fd);assert(!ftruncate(fd,4));
    assert(!unlink(control_path) && !symlink("/dev/null",control_path));check("marker",0,0,fd);
    assert(!unlink(control_path));check("marker",0,0,fd);close(fd);
    puts("async policy: default/overrides, live mapping, permissions, size and symlink checks passed");return 0;
}
