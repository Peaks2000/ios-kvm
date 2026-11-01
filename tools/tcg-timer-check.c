/* Diskless EL1 counter/timer regression: no iOS firmware or storage. */
#include <stdint.h>
char test_stack[16384] __attribute__((aligned(16)));
__attribute__((naked,section(".text.start"),used)) void _start(void) {
 __asm__ volatile("adrp x0, test_stack\nadd x0,x0,:lo12:test_stack\nadd sp,x0,#4,lsl #12\nbl check_main\nb .");
}
static uint64_t call(uint64_t op,const void *arg) {
 register uint64_t x0 __asm__("x0")=op;
 register const void *x1 __asm__("x1")=arg;
 __asm__ volatile("hlt #0xf000":"+r"(x0):"r"(x1):"memory"); return x0;
}
static void finish(int failed) {
 uint64_t status[]={0x20026,(uint64_t)failed};
 call(4,failed?"Counter/timer FAILED\n":"Counter/timer PASSED\n"); call(0x20,status);
 for(;;) __asm__ volatile("wfe");
}
#define CHECK(x) do { if(!(x)) finish(1); } while(0)
#define READ(reg) ({ uint64_t v; __asm__ volatile("mrs %0," #reg:"=r"(v)); v; })
#define WRITE(reg,v) __asm__ volatile("msr " #reg ",%0\nisb"::"r"((uint64_t)(v)):"memory")
volatile uint32_t last_irq;
extern char vectors[];
void check_main(void) {
 CHECK(READ(CurrentEL)==4);
 uint64_t freq=READ(CNTFRQ_EL0); CHECK(freq>=1000000 && freq<=10000000000ULL);
 uint64_t v=READ(CNTVCT_EL0), p=READ(CNTPCT_EL0);
 for(unsigned i=0;i<100000;i++) {
  uint64_t nv=READ(CNTVCT_EL0),np=READ(CNTPCT_EL0);
  CHECK(nv>=v && np>=p); CHECK(np>=nv && np-nv<freq/10);
  v=nv;p=np;
 }
 /* Masked virtual deadline: ISTATUS must change, then clear on re-arm. */
 uint64_t start=READ(CNTVCT_EL0),deadline=start+freq/50;
 WRITE(CNTV_CVAL_EL0,deadline); WRITE(CNTV_CTL_EL0,3);
 CHECK(!(READ(CNTV_CTL_EL0)&4));
 while(!(READ(CNTV_CTL_EL0)&4)) CHECK(READ(CNTVCT_EL0)-start<freq);
 CHECK(READ(CNTVCT_EL0)>=deadline);
 WRITE(CNTV_CVAL_EL0,READ(CNTVCT_EL0)+freq); CHECK(!(READ(CNTV_CTL_EL0)&4));
 WRITE(CNTV_CTL_EL0,0);
 /* Physical timer, relative negative TVAL means already expired. */
 WRITE(CNTP_TVAL_EL0,0xffffffffULL); WRITE(CNTP_CTL_EL0,3); CHECK(READ(CNTP_CTL_EL0)&4);
 WRITE(CNTP_CVAL_EL0,READ(CNTPCT_EL0)+freq/50); CHECK(!(READ(CNTP_CTL_EL0)&4));
 start=READ(CNTPCT_EL0);
 while(!(READ(CNTP_CTL_EL0)&4)) CHECK(READ(CNTPCT_EL0)-start<freq);
 WRITE(CNTP_CTL_EL0,0);
 /* Exercise delivery of the two timer PPIs through the emulated GICv2. */
 WRITE(VBAR_EL1,(uint64_t)vectors);
 volatile uint32_t *dist=(volatile uint32_t *)0x08000000;
 volatile uint32_t *gicc=(volatile uint32_t *)0x08010000;
 dist[0]=1; dist[0x100/4]=(1u<<27)|(1u<<30); gicc[1]=255; gicc[0]=1;
 for(unsigned which=0;which<2;which++) {
  last_irq=0;
  if(which) { WRITE(CNTP_CVAL_EL0,READ(CNTPCT_EL0)+freq/100); WRITE(CNTP_CTL_EL0,1); }
  else { WRITE(CNTV_CVAL_EL0,READ(CNTVCT_EL0)+freq/100); WRITE(CNTV_CTL_EL0,1); }
  start=READ(CNTVCT_EL0);
  __asm__ volatile("msr daifclr,#2":::"memory");
  while(!last_irq) CHECK(READ(CNTVCT_EL0)-start<freq);
  __asm__ volatile("msr daifset,#2":::"memory");
  CHECK((last_irq&1023)==(which?30:27));
 }
 call(4,"Virtual and physical timer IRQ delivery passed\n");
 call(4,"Monotonic counters, virtual/physical expiry and re-arm passed\n");
 finish(0);
}
