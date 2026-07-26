/* src/kernel/main.c */
#include "drivers/uart.h"
#include "kernel/mmu.h"
#include "kernel/exceptions.h"
#include "kernel/gic.h"
#include "kernel/timer.h"
#include "kernel/heap.h"
#include "kernel/scheduler.h"
#include "kernel/syscall.h"
#include "kernel/shell.h"


extern void rust_heap_init(void); 
extern void fs_init(void);

static void kprint_uint_inline(uint32_t n) {
    char buf[12];
    int pos = 11;
    buf[pos] = '\0';
    if (n == 0) { buf[--pos] = '0'; }
    while (n > 0) { buf[--pos] = '0' + (n % 10); n /= 10; }
    kprint(&buf[pos]);
}

void kernel_main(void) {
    mmu_init();
    uart_init();
    kprint("PurgatoryOS booting...\n");

    exceptions_init();
    kprint("Exceptions: ready\n");

    gic_init();
    kprint("GIC: ready\n");

    timer_init(500);
    kprint("Timer: armed\n");

    rust_heap_init();
    kprint("Heap: ready\n");

    fs_init();
    kprint("FS: mounted (ramdisk)\n");

    scheduler_init();
    syscall_init();
    kprint("Syscalls: ready\n");

    task_create(shell_main);

    asm volatile("msr daifclr, #2");
    kprint("Interrupts enabled.\n");

    kprint("Starting scheduler...\n");
    scheduler_start();
}
