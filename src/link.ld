ENTRY(_start)

SECTIONS {
    . = 0x40000000;

    /*
     * .text.boot must come first. _start must sit at exactly 0x40000000.
     * QEMU loads the ELF and jumps to ENTRY(), which is _start.
     * KEEP prevents the linker from discarding .text.boot even if it
     * looks unreferenced (it's referenced only by the ENTRY directive,
     * not by any C symbol).
     */
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    /*
     * .rodata: read-only data. String literals and const globals.
     * Separate from .text so the MMU can mark it readable but not
     * executable when we enable virtual memory in Post 6.
     */
    . = ALIGN(8);
    .rodata : {
        *(.rodata .rodata.*)
    }

    /*
     * .data: initialized read-write globals.
     * int counter = 42 → here. The values are stored in the ELF binary
     * and are already in RAM when QEMU loads us.
     */
    . = ALIGN(8);
    .data : {
        *(.data .data.*)
    }

    /*
     * .bss: zero-initialized globals.
     * C standard guarantees these are zero at program start. On bare
     * metal, boot.S must zero this region explicitly before calling
     * kernel_main. ALIGN(8) here is not optional. Our zeroing loop
     * uses 8-byte stores, which will alignment-fault on real hardware
     * if the start address isn't 8-byte aligned.
     *
     * *(COMMON) catches tentative definitions (e.g. int x; without
     * 'extern', in pre-C99 style). Include it or those may be silently
     * dropped.
     */
    . = ALIGN(8);
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);   /* guarantee 8-byte aligned end too */
        __bss_end = .;
    }

    /*
     * Stack: 16 KB, growing downward.
     * AArch64 uses a full-descending stack. SP points to the last
     * written word and decrements before each push. boot.S sets:
     *   ldr x0, =_stack_top
     *   mov sp, x0
     * so the stack starts at _stack_top (the high address) and grows
     * toward __stack_bottom. We don't put this in a named section .
     * it's just reserved address space with exported symbols.
     */
    . = ALIGN(16);          /* 16-byte stack alignment required by AAPCS64 */
    __stack_bottom = .;
    . += 0x4000;            /* 16 KB */
    _stack_top = .;

    /*
     * Heap: 1 MB placeholder for Post 8 (Heap Allocator).
     * The bump allocator will use __heap_start and __heap_end to
     * initialize its memory pool. By defining them here instead of
     * in C code, we keep the allocator decoupled from the memory layout.
     */
    . = ALIGN(8);
    __heap_start = .;
    . += 0x100000;          /* 1 MB */
    __heap_end = .;
}