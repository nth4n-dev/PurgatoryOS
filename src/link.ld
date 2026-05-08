ENTRY(_start)

SECTIONS {
    . = 0x40000000;

    .text : {
        *(.text.boot)
        *(.text)
    }

    .data : { *(.data) }

    .bss : {
        __bss_start = .;
        *(.bss)
        __bss_end = .;
    }

    /* Reserve 4KB for the stack */
    . = ALIGN(16);
    . += 4096;
    _stack_top = .;
}