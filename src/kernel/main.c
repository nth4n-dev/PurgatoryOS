/* src/kernel/main.c */
#include "drivers/uart.h"
#include "kernel/mmu.h"
#include "kernel/exceptions.h"
#include "kernel/gic.h"
#include "kernel/timer.h"
#include "kernel/heap.h"
#include "kernel/scheduler.h"
#include "kernel/syscall.h"


static void kprint_uint_inline(uint32_t n) {
    char buf[12];
    int pos = 11;
    buf[pos] = '\0';
    if (n == 0) { buf[--pos] = '0'; }
    while (n > 0) { buf[--pos] = '0' + (n % 10); n /= 10; }
    kprint(&buf[pos]);
}

/* Move strings to a user-data section (Read-Only) */
__attribute__((section(".rodata.user")))
static const char msg_a[] = "[task A] tick\n";

__attribute__((section(".rodata.user")))
static const char msg_b[] = "[task B] tick\n";

/* Move functions to a user-code section (Executable) */
__attribute__((section(".text.user")))
static void task_a(void) {
    while (1) {
        sys_write(1, msg_a, sizeof(msg_a) - 1);
        sys_yield();
    }
}

__attribute__((section(".text.user")))
static void task_b(void) {
    for (int i = 0; i < 5; i++) {
        sys_write(1, msg_b, sizeof(msg_b) - 1);
        sys_yield();
    }
    sys_exit(0);
}

/* Kernel entry */

/* Raw UART write usable before uart_init. QEMU PL011 at 0x09000000 */
static inline void raw_putc(char c) {
    *(volatile unsigned int *)0x09000000UL = (unsigned int)c;
}

void kernel_main(void) {
    raw_putc('A');   /* confirm kernel_main entered */
    mmu_init();
    raw_putc('B');   /* confirm mmu_init returned */
    uart_init();
    kprint("PurgatoryOS booting...\n");

    exceptions_init();
    kprint("Exceptions: ready\n");

    gic_init();
    kprint("GIC: ready\n");

    timer_init(500);
    kprint("Timer: armed\n");

    heap_init();
    kprint("Heap: ready\n");

    scheduler_init();

    syscall_init();
    kprint("Syscalls: ready\n");

    task_create(task_a);
    task_create(task_b);

    asm volatile("msr daifclr, #2");
    kprint("Interrupts enabled.\n");

    kprint("Starting scheduler...\n");
    scheduler_start();
}
