/* Pixel conversion comparison, native or freestanding AArch64 user-mode calculation. */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef IOS_KVM_FREESTANDING
static long call(long number,long a,long b,long c) {
    register long x0 __asm__("x0")=a,x1 __asm__("x1")=b,x2 __asm__("x2")=c,x8 __asm__("x8")=number;
    __asm__ volatile("svc 0":"+r"(x0):"r"(x1),"r"(x2),"r"(x8):"memory"); return x0;
}
static void output(const char *s,size_t n) { call(64,1,(long)s,n); }
static void done(int status) { call(93,status,0,0); __builtin_unreachable(); }
static uint64_t now(void) {
    struct {long sec,nsec;} t;
    if(call(113,1,(long)&t,0)) done(1);
    return (uint64_t)t.sec*1000000+t.nsec/1000;
}
#else
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
static void output(const char *s,size_t n) { if(write(1,s,n)!=(ssize_t)n) exit(1); }
static void done(int status) { exit(status); }
static uint64_t now(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)) done(1);
    return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
#endif
#include "../vendor/hakios/userland/src/surface_pixels.h"
static unsigned char input[750*1624*4+64],output_old[sizeof(input)],output_new[sizeof(input)];
static void say(const char *s) { size_t n=0;while(s[n])n++;output(s,n); }
static void number(uint64_t n) { char s[24];unsigned i=sizeof(s);do{s[--i]='0'+n%10;n/=10;}while(n);output(s+i,sizeof(s)-i); }
static void check(int valid) { if(!valid){say("pixel conversion check failed\n");done(1);} }
static void original(unsigned char *to,const unsigned char *from,size_t pixels,bool bgra) {
    for(size_t i=0;i<pixels*4;i+=4) {
        to[i]=from[i+(bgra?1:3)];to[i+1]=from[i+2];to[i+2]=from[i+(bgra?3:1)];to[i+3]=from[i];
    }
}
static int run(void) {
    for(size_t i=0;i<sizeof(input);i++)input[i]=(unsigned char)(i*37+11);
    for(unsigned src=0;src<8;src++)for(unsigned dst=0;dst<8;dst++)for(size_t pixels=0;pixels<=257;pixels++)for(unsigned bgra=0;bgra<2;bgra++) {
        for(size_t i=0;i<1100;i++)output_old[i]=output_new[i]=0xa5;
        original(output_old+dst,input+src,pixels,bgra);
        surface_alpha_first_row(output_new+dst,input+src,pixels,bgra);
        for(size_t i=0;i<1100;i++)check(output_old[i]==output_new[i]);
    }
    say("pixel conversion checks passed: both formats, all source/destination offsets0..7, lengths0..257, guards\n");
    for(unsigned bgra=0;bgra<2;bgra++)for(int phase=0;phase<4;phase++) {
        bool fast=phase==1||phase==2;uint64_t samples[16];size_t pixels=750*1624;
        for(int run=0;run<20;run++) {
            input[run]++;uint64_t began=now();
            for(size_t row=0;row<1624;row++) {
                size_t source_row=(run&1)?1623-row:row;
                if(fast)surface_alpha_first_row(output_new+1+row*750*4,input+1+source_row*750*4,750,bgra);
                else original(output_new+1+row*750*4,input+1+source_row*750*4,750,bgra);
            }
            uint64_t elapsed=now()-began;
            for(size_t row=0;row<1624;row++) {
                size_t source_row=(run&1)?1623-row:row;
                original(output_old+1+row*750*4,input+1+source_row*750*4,750,bgra);
            }
            for(size_t i=0;i<pixels*4;i++)check(output_new[i+1]==output_old[i+1]);
            if(run>=4)samples[run-4]=elapsed;
        }
        for(int i=1;i<16;i++)for(int j=i;j>0&&samples[j]<samples[j-1];j--){uint64_t t=samples[j];samples[j]=samples[j-1];samples[j-1]=t;}
        say("pixel conversion bench bgra=");number(bgra);say(" phase=");number(phase);say(" word=");number(fast);
        say(" median_us=");number((samples[7]+samples[8])/2);say("\n");
    }
    return 0;
}
#ifdef IOS_KVM_FREESTANDING
void _start(void) { done(run()); }
#else
int main(void) { return run(); }
#endif
