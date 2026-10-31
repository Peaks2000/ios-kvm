/* Correctness and ABBA timing of the production IOSurface fingerprint. No VM required. */
#include <stddef.h>
#include <stdint.h>
#ifdef IOS_KVM_FREESTANDING
static long call(long number, long a, long b, long c)
{
    register long x0 __asm__("x0")=a, x1 __asm__("x1")=b, x2 __asm__("x2")=c;
    register long x8 __asm__("x8")=number;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x8) : "memory");
    return x0;
}
static void output(const char *s, size_t length) { call(64,1,(long)s,length); }
static void done(int status) { call(93,status,0,0); __builtin_unreachable(); }
static uint64_t now(void) {
    struct {long seconds,nanoseconds;} t;
    if (call(113,1,(long)&t,0)) done(1);
    return (uint64_t)t.seconds*1000000+t.nanoseconds/1000;
}
#else
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
static void output(const char *s, size_t length) { if (write(1,s,length)!=(ssize_t)length) exit(1); }
static void done(int status) { exit(status); }
static uint64_t now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) done(1);
    return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
#endif
#include "../vendor/hakios/userland/src/surface_fingerprint.h"
static unsigned char data[750*1624*4+65];
static volatile uint64_t sink;
static void say(const char *s) { size_t n=0; while(s[n]) n++; output(s,n); }
static void number(uint64_t n) {
    char s[24]; unsigned i=sizeof(s);
    do { s[--i]='0'+n%10; n/=10; } while(n);
    output(s+i,sizeof(s)-i);
}
static void check(int condition) { if (!condition) { say("fingerprint check failed\n"); done(1); } }
static uint64_t original(const void *bytes,size_t size) {
    const unsigned char *base=bytes; uint64_t sum=size,word;
    for (size_t at=0;at+8<=size;at+=8) {
        __builtin_memcpy(&word,base+at,8);
        sum=(sum^word)*UINT64_C(0x9e3779b97f4a7c15); sum^=sum>>29;
    }
    return sum;
}
static int run(void)
{
    for(size_t i=0;i<sizeof(data);i++) data[i]=(unsigned char)(i*37+11);
    check(surface_fingerprint(data,0)==UINT64_C(0xef46db3751d8e999));
    for (size_t offset=0;offset<8;offset++) for(size_t size=1;size<=257;size++) {
        uint64_t before=surface_fingerprint(data+offset,size);
        check(before==surface_fingerprint(data+offset,size));
        for(size_t byte=0;byte<size;byte++) {
            data[offset+byte]^=0x80;
            check(before!=surface_fingerprint(data+offset,size));
            data[offset+byte]^=0x80;
        }
    }
    size_t bytes=750*1624*4+3;
    uint64_t before=surface_fingerprint(data+1,bytes);
    data[bytes]^=1; check(before!=surface_fingerprint(data+1,bytes)); data[bytes]^=1;
    say("fingerprint checks passed: deterministic, unaligned, every byte at lengths1..257, large final tail\n");
    for (int phase=0;phase<4;phase++) {
        int fast=phase==1 || phase==2; uint64_t samples[16];
        for(int sample=0;sample<20;sample++) {
            data[sample]^=1;
            uint64_t began=now();
            uint64_t value=fast?surface_fingerprint(data,bytes):original(data,bytes);
            uint64_t elapsed=now()-began; sink=value;
            if(sample>=4) samples[sample-4]=elapsed;
        }
        for(int i=1;i<16;i++) for(int j=i;j>0 && samples[j]<samples[j-1];j--) {
            uint64_t t=samples[j]; samples[j]=samples[j-1]; samples[j-1]=t;
        }
        say("fingerprint bench phase=");number(phase);say(" xxh64=");number(fast);
        say(" bytes=");number(bytes);say(" median_us=");number((samples[7]+samples[8])/2);say("\n");
    }
    return 0;
}
#ifdef IOS_KVM_FREESTANDING
void _start(void) { done(run()); }
#else
int main(void) { return run(); }
#endif
