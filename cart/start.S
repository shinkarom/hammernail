.section .text.init, "ax"
.global _start
.type _start, @function

_start:
    /* Disable machine interrupts on boot */
    csrw mie, zero
    csrw mstatus, zero

    /* Initialize Global Pointer (gp) for small-data relaxation */
.option push
.option norelax
    la gp, __global_pointer$
.option pop

    /* Initialize Stack Pointer (sp = 0x84000000) */
    la sp, __stack_top

    /* Clear .bss section (8 bytes at a time) */
    la t0, __bss_start
    la t1, __bss_end
    bgeu t0, t1, 2f
1:
    sd zero, 0(t0)
    addi t0, t0, 8
    bltu t0, t1, 1b
2:
    /* Jump to C/Zig main(argc=0, argv=NULL) */
    li a0, 0
    li a1, 0
    call main

    /* If main returns, sleep forever by yielding frames */
halt_loop:
    li a7, 1        /* SYS_vblank_yield */
    ecall
    j halt_loop