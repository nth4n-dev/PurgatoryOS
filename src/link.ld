ENTRY(_start)

SECTIONS {
    /* RAM starts at 0x40000000 */
    . = 0x40000000;

    /* --- BLOCK 0: KERNEL (0x40000000 - 0x401FFFFF) --- */
    /* Mapped as BLOCK_NORMAL (EL1 Only) in mmu.c */
    .text : {
        KEEP(*(.text.boot))
        KEEP(*(.text.vectors))
        /* CRITICAL: Exclude user-related objects from the kernel's text block */
        *(EXCLUDE_FILE(*user.o *main.o) .text EXCLUDE_FILE(*user.o *main.o) .text.*)
    }

    .rodata : { 
        /* Exclude main.o from here too so the task strings don't land in Block 0 */
        *(EXCLUDE_FILE(*main.o) .rodata EXCLUDE_FILE(*main.o) .rodata.*) 
    }
    
    .data : { *(.data .data.*) }

    . = ALIGN(4096);
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(16);
    __stack_bottom = .;
    . += 0x4000;
    _stack_top = .;

    /* --- BLOCK 1: USER (0x40200000 - 0x403FFFFF) --- */
    /* Align to the 2MB L2 boundary to match mmu.c's l2_table[1] */
    . = ALIGN(0x200000); 

    .user_space : {
        _user_start = .;
        /* Explicitly place the task code and data from main.o here */
        build/src/kernel/main.o(.text.user)
        build/src/kernel/main.o(.rodata.user)
        /* Explicitly place the syscall assembly wrappers here */
        build/src/user/user.o(.text .text.*)
        _user_end = .;
    }

    . = ALIGN(8);
    __heap_start = .;
    . += 0x100000;
    __heap_end = .;
}
