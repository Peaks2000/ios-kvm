/* Fixed ARM workloads for comparing emulator builds without booting a VM. */
#include <stddef.h>
#include <stdint.h>
#ifdef IOS_KVM_FREESTANDING
static long call(long number,long a,long b,long c) {
    register long x0 __asm__("x0")=a,x1 __asm__("x1")=b,x2 __asm__("x2")=c,x8 __asm__("x8")=number;
    __asm__ volatile("svc 0":"+r"(x0):"r"(x1),"r"(x2),"r"(x8):"memory");return x0;
}
static void output(const char *s,size_t n) { call(64,1,(long)s,n); }
static void done(int status) { call(93,status,0,0);__builtin_unreachable(); }
static uint64_t now(void) {
    struct {long sec,nsec;} t;
    if(call(113,1,(long)&t,0))done(1);
    return (uint64_t)t.sec*1000000+t.nsec/1000;
}
#else
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
static void output(const char *s,size_t n) { if(write(1,s,n)!=(ssize_t)n)exit(1); }
static void done(int status) { exit(status); }
static uint64_t now(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t))done(1);
    return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
#endif
static uint32_t memory[1<<20];
static volatile uint64_t sink;
static void say(const char *s) { size_t n=0;while(s[n])n++;output(s,n); }
static void number(uint64_t n) { char s[24];unsigned i=sizeof(s);do{s[--i]='0'+n%10;n/=10;}while(n);output(s+i,sizeof(s)-i); }
__attribute__((noinline)) static uint64_t dependent(uint64_t x) {
    for(unsigned i=0;i<20000000;i++) {
        x^=x>>12;x^=x<<25;x^=x>>27;x*=UINT64_C(2685821657736338717);
    }
    return x;
}
__attribute__((noinline)) static uint64_t step0(uint64_t x) { return ((x<<7)|(x>>57))^UINT64_C(0x9e3779b97f4a7c15); }
__attribute__((noinline)) static uint64_t step1(uint64_t x) { return x*UINT64_C(0xbf58476d1ce4e5b9)+17; }
__attribute__((noinline)) static uint64_t step2(uint64_t x) { return (x^(x>>31))-UINT64_C(0x94d049bb133111eb); }
__attribute__((noinline)) static uint64_t step3(uint64_t x) { return ((x>>11)|(x<<53))^(x*3); }
typedef uint64_t (*work_fn)(uint64_t);
static work_fn volatile steps[]={step0,step1,step2,step3};
__attribute__((noinline)) static uint64_t dispatch(uint64_t x) {
    for(unsigned i=0;i<1000000;i++)x=steps[x&3](x);
    return x;
}
#ifdef IOS_KVM_MANY_TARGETS
#include "tcg-many-targets.inc"
#endif
__attribute__((noinline)) static uint64_t chase(uint64_t x) {
    uint32_t at=(uint32_t)x;uint64_t sum=0;
    for(unsigned i=0;i<8000000;i++){at=memory[at];sum+=at;}
    return sum^((uint64_t)at<<32);
}
static int run(void) {
    for(uint32_t i=0;i<(1<<20);i++)memory[i]=(i*1664525U+1013904223U)&((1<<20)-1);
    work_fn functions[]={dependent,dispatch,chase
#ifdef IOS_KVM_MANY_TARGETS
        ,dispatch_many
#endif
    };
    const char *names[]={"dependent","indirect","memory"
#ifdef IOS_KVM_MANY_TARGETS
        ,"many_targets"
#endif
    };
    for(unsigned work=0;work<sizeof(functions)/sizeof(functions[0]);work++)for(unsigned sample=0;sample<8;sample++) {
        uint64_t began=now(),checksum=functions[work](17),elapsed=now()-began;sink=checksum;
        say("tcg-cpu: workload=");say(names[work]);say(" sample=");number(sample);
        say(" elapsed_us=");number(elapsed);say(" checksum=");number(checksum);say("\n");
    }
    return 0;
}
#ifdef IOS_KVM_FREESTANDING
void _start(void) { done(run()); }
#else
int main(void) { return run(); }
#endif
