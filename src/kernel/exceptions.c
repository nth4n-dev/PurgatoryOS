/* src/kernel/exceptions.c */
#include <stdint.h>
#include "drivers/uart.h"
#include "kernel/exceptions.h"
#include "kernel/gic.h"
#include "kernel/timer.h"

extern void vectors(void);

void exceptions_init(void) {
    /* Load the address of our vector table into VBAR_EL1.
     * Any exception taken after this instruction uses our table.
     * The isb() ensures no subsequent instructions execute before
     * this register write is visible to the CPU's fetch pipeline. */
    asm volatile(
        "msr vbar_el1, %0\n"
        "isb\n"
        :: "r"((uint64_t)vectors)
        : "memory"
    );
}

static void print_hex(uint64_t val) {
    const char hex[] = "0123456789abcdef";
    kprint("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xF]);
    }
}

void sync_exception_handler(void) {
    uint64_t esr, elr, far;

    asm volatile("mrs %0, esr_el1"  : "=r"(esr));
    asm volatile("mrs %0, elr_el1"  : "=r"(elr));
    asm volatile("mrs %0, far_el1"  : "=r"(far));

    uint32_t ec  = (esr >> 26) & 0x3F;
    uint32_t iss = esr & 0x01FFFFFF;

    kprint("\n[SYNC EXCEPTION]\n");
    kprint("  ESR_EL1 = "); print_hex(esr);  kprint("\n");
    kprint("  ELR_EL1 = "); print_hex(elr);  kprint("\n");
    kprint("  FAR_EL1 = "); print_hex(far);  kprint("\n");
    kprint("  EC      = "); print_hex(ec);   kprint("\n");
    kprint("  ISS     = "); print_hex(iss);  kprint("\n");

    /* Spin. We cannot recover from an unhandled fault. */
    while (1);
}

void irq_handler(void) {
    /* Acknowledge the interrupt. This tells the GIC we have seen it and
     * returns the INTID of the highest-priority pending interrupt. */
    uint32_t iar = gic_acknowledge();
    uint32_t irq = iar & 0x3FF;   /* INTID lives in bits [9:0] */

    if (irq == TIMER_IRQ) {
        timer_tick();
    } else {
        /* Unknown interrupt. Acknowledge it so the GIC does not stall,
         * but log a warning. Future drivers will register handlers here. */
        kprint("[irq] unhandled INTID: ");
        /* (printing the IRQ number is left out here) */
    }

    /* Signal end-of-interrupt. The GIC only forwards the next interrupt
     * of this priority after we write here. Forgetting this call
     * means we receive exactly one interrupt and then silence. */
    gic_end(iar);
}
