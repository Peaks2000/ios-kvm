/* Disposable ARM system-mode MMU/code-invalidation check. No firmware or disks. */
#include <stdint.h>

static uint64_t l1[512] __attribute__((aligned(4096)));
static uint64_t l2[512] __attribute__((aligned(4096)));
static uint64_t l3[512] __attribute__((aligned(4096)));
static uint32_t code[2][1024] __attribute__((aligned(4096)));
char test_stack[16384] __attribute__((aligned(16)));

__attribute__((naked,section(".text.start"),used)) void _start(void)
{
    __asm__ volatile("adrp x0, test_stack\nadd x0, x0, :lo12:test_stack\n"
                     "add sp, x0, #4, lsl #12\nbl check_main\nb .");
}

static uint64_t hostcall(uint64_t operation, const void *argument)
{
    register uint64_t x0 __asm__("x0")=operation;
    register const void *x1 __asm__("x1")=argument;
    __asm__ volatile("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
    return x0;
}

static void finish(int failed)
{
    uint64_t status[]={0x20026,(uint64_t)failed};
    hostcall(4,failed?"TCG system check FAILED\n":"TCG system check PASSED\n");
    hostcall(0x20,status);
    for (;;) __asm__ volatile("wfe");
}

static void check(int condition)
{
    if (!condition) finish(1);
}

static void define(volatile uint32_t *at, unsigned value)
{
    at[0]=UINT32_C(0xd2800000)|(value<<5); /* MOVZ X0, #value */
    at[1]=UINT32_C(0xd65f03c0);            /* RET */
    __asm__ volatile("dc cvau, %0\ndsb ish\nic ivau, %0\ndsb ish\nisb"
                     : : "r"(at) : "memory");
}

typedef uint64_t (*code_fn)(void);

void check_main(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    check(el==4); /* virt without virtualization starts this ELF in EL1. */
    define(code[0],111); define(code[1],222);
    for (unsigned i=0;i<512;i++) {
        define(code[0],300+i);
        check(((code_fn)code[0])()==300+i);
        check(((code_fn)code[1])()==222);
    }
    hostcall(4,"TCG system: physical modified code passed\n");

    /* 39-bit VA, 4 KiB granule: identity-map low 512 GiB with 1 GiB blocks,
     * then split RAM's block to add a remappable alias at VA 0x50000000. */
    for (unsigned i=0;i<512;i++) {
        l1[i]=((uint64_t)i<<30)|0x701; /* AF, inner shareable, normal MAIR[0] */
        l2[i]=(UINT64_C(0x40000000)+((uint64_t)i<<21))|0x701;
    }
    l1[1]=(uint64_t)l2|3;
    l2[128]=(uint64_t)l3|3;
    l3[0]=(uint64_t)code[0]|0x703;
    uint64_t tcr=(UINT64_C(2)<<32)|(3<<12)|(1<<10)|(1<<8)|25;
    uint64_t mair=0xff, sctlr;
    __asm__ volatile("msr mair_el1, %0\nmsr tcr_el1, %1\nmsr ttbr0_el1, %2\n"
                     "dsb sy\nisb"
                     : : "r"(mair), "r"(tcr), "r"(l1) : "memory");
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr|=1;
    __asm__ volatile("msr sctlr_el1, %0\nisb" : : "r"(sctlr) : "memory");
    code_fn alias=(code_fn)UINT64_C(0x50000000);
    uint64_t page=UINT64_C(0x50000000)>>12;
    check(alias()==811);
    for (unsigned i=0;i<512;i++) {
        define(code[0],1000+i);
        check(alias()==1000+i && ((code_fn)code[1])()==222);
    }
    hostcall(4,"TCG system: aliased modified code passed\n");
    for (unsigned i=0;i<512;i++) {
        unsigned slot=i&1;
        /* Break-before-make with architected EL1 TLB invalidation. */
        l3[0]=0;
        __asm__ volatile("dsb ishst\ntlbi vae1is, %0\ndsb ish\nisb" : : "r"(page) : "memory");
        l3[0]=(uint64_t)code[slot]|0x703;
        __asm__ volatile("dsb ishst\nisb" : : : "memory");
        check(alias()==(slot?222:1511));
    }
    hostcall(4,"TCG system: virtual-address remapping passed\n");
    finish(0);
}
