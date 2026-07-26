/* src/kernel/main.c */
#include "drivers/uart.h"
#include "kernel/mmu.h"
#include "kernel/exceptions.h"
#include "kernel/gic.h"
#include "kernel/timer.h"
#include "kernel/heap.h"
#include "kernel/scheduler.h"


static void kprint_uint_inline(uint32_t n) {
    char buf[12];
    int pos = 11;
    buf[pos] = '\0';
    if (n == 0) { buf[--pos] = '0'; }
    while (n > 0) { buf[--pos] = '0' + (n % 10); n /= 10; }
    kprint(&buf[pos]);
}

static void task_a(void) {
    uint32_t count = 0;
    while (1) {
        kprint("[task A] count=");
        kprint_uint_inline(count++);
        kprint("\n");
        scheduler_yield();
    }
}

static void task_b(void) {
    uint32_t count = 0;
    while (1) {
        kprint("[task B] count=");
        kprint_uint_inline(count++);
        kprint("\n");
        scheduler_yield();
    }
}

/* Kernel entry */

void kernel_main(void) {
    mmu_init();
    uart_init();

    kprint("PurgatoryOS booting...\n");

    /* Exception vectors. Install VBAR_EL1 before we unmask interrupts */
    exceptions_init();
    kprint("Exceptions: ready\n");

    /* GIC. Enable the distributor and CPU interface */
    gic_init();
    kprint("GIC: ready\n");

    /* Timer. Fires every 500 ms, calls scheduler_tick via timer_tick() */
    timer_init(500);
    kprint("Timer: armed\n");

    /* Heap. Must be up before any kmalloc in scheduler or task_create */
    heap_init();
    kprint("Heap: ready\n");

    /* Scheduler. Initialise the runqueue before creating tasks */
    scheduler_init();

    /* Create the two initial tasks.
     * task_create allocates a PCB + stack from the heap and sets up
     * the initial cpu_context_t so the first context switch calls entry(). */
    task_create(task_a);
    task_create(task_b);

    /* Unmask IRQs at the CPU so the timer interrupt can preempt tasks */
    asm volatile("msr daifclr, #2");
    kprint("Interrupts enabled.\n");

    /* Hand control to the scheduler permanently.
     * This call never returns. Kernel_main's stack is abandoned. */
    kprint("Starting scheduler...\n");
    scheduler_start();

    /* Unreachable. Here for safety */
    while (1) { asm volatile("wfi"); }
}
