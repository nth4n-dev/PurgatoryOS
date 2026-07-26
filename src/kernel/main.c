/* src/kernel/main.c */
#include "drivers/uart.h"
#include "kernel/mmu.h"
#include "kernel/exceptions.h"
#include "kernel/gic.h"
#include "kernel/timer.h"

void kernel_main(void) {
    mmu_init();
    uart_init();

    kprint("PurgatoryOS booting...\n");

    /* Exception vectors. Install VBAR_EL1 before we unmask interrupts. */
    exceptions_init();
    kprint("Exceptions: ready\n");

    /* GIC: enable the distributor and the CPU interface. */
    gic_init();
    kprint("GIC: ready\n");

    /* Timer: 500 ms interval, fires INTID 30. */
    timer_init(500);
    kprint("Timer: armed\n");

    /* Unmask IRQs at the CPU. From here on, interrupts can fire. */
    asm volatile("msr daifclr, #2");   /* clear IRQ mask bit in PSTATE */

    kprint("Interrupts enabled. Waiting for timer...\n");

    while (1) {
        /* wfi means Wait For Interrupt. It puts the CPU into a low-power state
         * until an interrupt arrives. Much better than a busy spin. */
        asm volatile("wfi");
    }
}
