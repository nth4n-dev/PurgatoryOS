ENTRY(_start)

SECTIONS {
    /* QEMU virt RAM starts at 0x40000000 */
    . = 0x40000000;

    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    . = ALIGN(8);
    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(8);
    .data : {
        *(.data .data.*)
    }

    /* * Page tables are static in .bss.
     * TTBR0_EL1 requires the table address to be page-aligned (4KB).
     */
    . = ALIGN(4096);
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4096);
        __bss_end = .;
    }

    . = ALIGN(16);
    __stack_bottom = .;
    . += 0x4000;            /* 16 KB Stack */
    _stack_top = .;

    . = ALIGN(8);
    __heap_start = .;
    . += 0x100000;          /* 1 MB Heap */
    __heap_end = .;
}
