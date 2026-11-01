/* Freestanding AArch64 Linux cache-invalidation checks. No firmware or VM. */
#include <stddef.h>
#include <stdint.h>

static long syscall6(long number, long a, long b, long c, long d, long e, long f)
{
    register long x0 __asm__("x0")=a, x1 __asm__("x1")=b, x2 __asm__("x2")=c;
    register long x3 __asm__("x3")=d, x4 __asm__("x4")=e, x5 __asm__("x5")=f;
    register long x8 __asm__("x8")=number;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5),"r"(x8) : "memory");
    return x0;
}

static void finish(int status)
{
    syscall6(93,status,0,0,0,0,0);
    __builtin_unreachable();
}

static void say(const char *text)
{
    size_t length=0;
    while (text[length]) length++;
    if (syscall6(64,1,(long)text,length,0,0,0)!=(long)length) finish(1);
}

static void check(int condition)
{
    if (!condition) {
        say("TCG invalidation check failed\n");
        finish(1);
    }
}

static void flush(void *memory, size_t bytes)
{
    uintptr_t first=(uintptr_t)memory, last=first+bytes;
    /* Four-byte steps cover both instructions and any crossed cache line. */
    for (uintptr_t at=first;at<last;at+=4)
        __asm__ volatile("dc cvau, %0" : : "r"(at) : "memory");
    __asm__ volatile("dsb ish" : : : "memory");
    for (uintptr_t at=first;at<last;at+=4)
        __asm__ volatile("ic ivau, %0" : : "r"(at) : "memory");
    __asm__ volatile("dsb ish\nisb" : : : "memory");
}

typedef uint64_t (*generated_fn)(void);

static void define(volatile uint32_t *code, unsigned value)
{
    code[0]=UINT32_C(0xd2800000)|(value<<5); /* MOVZ X0, #value */
    code[1]=UINT32_C(0xd65f03c0);            /* RET */
    flush((void *)code,8);
}

static void collision_checks(void)
{
    const long first=0x4000000000L, second=first+(1L<<24);
    const size_t page=4096;
    /* These distinct executable pages collide in QEMU's 12-bit user cache.
     * Alternating them populates both ways before modification/remapping. */
    check(((first^(first>>12))&4095)==((second^(second>>12))&4095));
    long a=syscall6(222,first,page,7,0x100022,-1,0);
    long b=syscall6(222,second,page,7,0x100022,-1,0);
    check(a==first && b==second);
    generated_fn run_a=(generated_fn)a, run_b=(generated_fn)b;
    define((void *)a,1000); define((void *)b,2000);
    for (unsigned i=0;i<1024;i++) check(run_a()==1000 && run_b()==2000);
    for (unsigned i=0;i<128;i++) {
        define((void *)a,1001+i);
        check(run_b()==2000 && run_a()==1001+i);
    }
    check(!syscall6(215,b,page,0,0,0,0));
    check(syscall6(222,b,page,7,0x100022,-1,0)==b);
    define((void *)b,3000);
    for (unsigned i=0;i<1024;i++) check(run_a()==1128 && run_b()==3000);
    check(!syscall6(215,a,page,0,0,0,0));
    check(!syscall6(215,b,page,0,0,0,0));
    say("TCG invalidation: colliding cache entries modified/remapped correctly\n");
}

void _start(void)
{
    collision_checks();
    const size_t page=4096, size=page*2;
    long result=syscall6(222,0,size,7,0x22,-1,0); /* mmap anonymous RWX */
    check((uint64_t)result<(uint64_t)-4095);
    unsigned char *memory=(void *)result;
    volatile uint32_t *a=(void *)(memory+64), *b=(void *)(memory+page+64);
    generated_fn run_a=(generated_fn)(memory+64), run_b=(generated_fn)(memory+page+64);
    define(a,111); define(b,222);
    check(run_a()==111 && run_b()==222);
    for (unsigned i=0;i<512;i++) {
        unsigned value=(i&1)?113:112;
        define(a,value);
        check(run_a()==value && run_b()==222);
    }
    say("TCG invalidation: same-page modification passed\n");

    /* The ADD and RET are in the second page; modify only that page. */
    volatile uint32_t *cross=(void *)(memory+page-4);
    generated_fn run_cross=(generated_fn)(memory+page-4);
    cross[0]=UINT32_C(0xd2800000)|(333<<5);
    cross[1]=UINT32_C(0x91000400); /* ADD X0, X0, #1 */
    cross[2]=UINT32_C(0xd65f03c0);
    flush((void *)cross,12);
    check(run_cross()==334);
    for (unsigned i=0;i<256;i++) {
        unsigned increment=(i&1)?3:1;
        cross[1]=UINT32_C(0x91000000)|(increment<<10);
        flush((void *)(cross+1),4);
        check(run_cross()==333+increment && run_b()==222);
    }
    say("TCG invalidation: page-crossing modification passed\n");

    for (unsigned i=0;i<64;i++) {
        check(!syscall6(226,(long)memory,page,5,0,0,0)); /* mprotect RX */
        check(run_a()==((i==0)?113:400+i-1));
        check(!syscall6(226,(long)memory,page,7,0,0,0)); /* mprotect RWX */
        define(a,400+i);
        check(run_a()==400+i && run_b()==222);
    }
    say("TCG invalidation: protection changes passed\n");
    check(!syscall6(215,(long)memory,size,0,0,0,0));
    result=syscall6(222,(long)memory,size,7,0x32,-1,0); /* mmap MAP_FIXED at old address */
    check(result==(long)memory);
    define((void *)(memory+64),777);
    define((void *)(memory+page+64),888);
    check(run_a()==777 && run_b()==888);
    check(!syscall6(215,(long)memory,size,0,0,0,0));
    say("TCG invalidation: address reuse passed; all checks passed\n");
    finish(0);
}
