.section .text
.balign 2048
.global vectors
vectors:
.rept 4
 b .
 .balign 128
.endr
 b .
 .balign 128
 b irq_handler
 .balign 128
.rept 10
 b .
 .balign 128
.endr
irq_handler:
 stp x0,x1,[sp,#-16]!
 stp x2,x3,[sp,#-16]!
 mov x0,#0x8010000
 ldr w1,[x0,#12]
 adrp x2,last_irq
 add x2,x2,:lo12:last_irq
 str w1,[x2]
 msr cntv_ctl_el0,xzr
 msr cntp_ctl_el0,xzr
 str w1,[x0,#16]
 dsb sy
 ldp x2,x3,[sp],#16
 ldp x0,x1,[sp],#16
 eret
