OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    /* 2 MB ROM + Code/Data space (0x80000000 - 0x801FFFFF) */
    ROM_RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 2M

    /* 0x80200000 - 0x803FFFFF is reserved for VRAM & MMIO */

    /* 60 MB Heap & Stack space (0x80400000 - 0x84000000) */
    HEAP_RAM (rw) : ORIGIN = 0x80400000, LENGTH = 60M
}

SECTIONS
{
    . = ORIGIN(ROM_RAM);

    /* _start MUST be placed at the very first byte (0x80000000) */
    .text : ALIGN(4) {
        KEEP(*(.text.init))
        *(.text .text.*)
    } > ROM_RAM

    .rodata : ALIGN(8) {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > ROM_RAM

    .data : ALIGN(8) {
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        *(.data .data.*)
    } > ROM_RAM

    /* NOLOAD ensures .bss doesn't bloat the flat .rom binary file */
    .bss (NOLOAD) : ALIGN(8) {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > ROM_RAM

    /* Symbols for C/Zig allocators and startup */
    __heap_start = ORIGIN(HEAP_RAM);
    __heap_end   = ORIGIN(HEAP_RAM) + LENGTH(HEAP_RAM) - 64K;
    __stack_top  = ORIGIN(HEAP_RAM) + LENGTH(HEAP_RAM); /* 0x84000000 */

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}