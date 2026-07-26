ENTRY(_start)

SECTIONS {
    /* RAM starts at 0x40000000 */
    . = 0x40000000;

    /* --- BLOCK 0: KERNEL (0x40000000 - 0x401FFFFF) --- */
    /* Mapped as BLOCK_NORMAL (EL1 Only) in mmu.c */
    .text : {
        KEEP(*(.text.boot))
        KEEP(*(.text.vectors))
        /* Exclude user-space objects so they land in BLOCK 1 below */
        *(EXCLUDE_FILE(*user.o *main.o *shell.o) .text EXCLUDE_FILE(*user.o *main.o *shell.o) .text.*)
    }

    .rodata : {
        /* Exclude main.o and shell.o. Their rodata goes into user space */
        *(EXCLUDE_FILE(*main.o *shell.o) .rodata EXCLUDE_FILE(*main.o *shell.o) .rodata.*)
    }

    .data : {
        /* Exclude shell.o. Sh_cwd and other mutable data go into user space */
        *(EXCLUDE_FILE(*shell.o) .data EXCLUDE_FILE(*shell.o) .data.*)
    }

    . = ALIGN(4096);
    .bss : {
        __bss_start = .;
        *(EXCLUDE_FILE(*shell.o) .bss EXCLUDE_FILE(*shell.o) .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(16);
    __stack_bottom = .;
    . += 0x4000;
    _stack_top = .;

    /* --- BLOCK 1: USER (0x40200000 - 0x403FFFFF) --- */
    /* Aligned to the 2 MB L2 boundary to match mmu.c's l2_ram[1] entry */
    . = ALIGN(0x200000);

    .user_space : {
        _user_start = .;
        /* shell: all code, read-only data, and mutable data */
        build/src/kernel/shell.o(.text .text.*)
        build/src/kernel/shell.o(.rodata .rodata.*)
        build/src/kernel/shell.o(.data .data.*)
        build/src/kernel/shell.o(.bss .bss.*)
        /* syscall SVC wrappers */
        build/src/user/user.o(.text .text.*)
        _user_end = .;
    }

    . = ALIGN(8);
    __heap_start = .;
    . += 0x100000;
    __heap_end = .;
}
