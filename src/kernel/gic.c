/* src/kernel/gic.c */
#include "kernel/gic.h"

void gic_init(void) {
    /* 1. Enable the distributor. It forwards interrupts to the CPU interfaces. */
    GICD_REG(GICD_CTLR) = 1;

    /* 2. Set the CPU Interface Priority Mask.
     * A value of 0xFF means "accept any priority level."
     * GIC priorities are backwards: 0x00 = highest, 0xFF = lowest.
     * Without this, all interrupts are masked regardless of configuration. */
    GICC_REG(GICC_PMR) = 0xFF;

    /* 3. Enable the CPU interface so it can signal the CPU. */
    GICC_REG(GICC_CTLR) = 1;
}

void gic_enable_irq(uint32_t irq) {
    /* GICD_ISENABLER is an array of 32-bit registers, one bit per IRQ.
     * IRQ n → register index n/32, bit n%32. */
    uint32_t reg_idx = irq / 32;
    uint32_t bit     = irq % 32;
    GICD_REG(GICD_ISENABLER + reg_idx * 4) = (1U << bit);
}

uint32_t gic_acknowledge(void) {
    /* Reading GICC_IAR atomically acknowledges the interrupt and returns
     * the INTID in bits [9:0]. The remaining bits encode the CPU source ID.
     * We return the full IAR value because GICC_EOIR needs it. */
    return GICC_REG(GICC_IAR);
}

void gic_end(uint32_t iar) {
    /* Writing the IAR value back to GICC_EOIR signals to the GIC that
     * we are done handling this interrupt. The GIC can then forward the
     * next pending interrupt of the same priority. Forgetting this write
     * silently prevents all future interrupts of this priority or lower. */
    GICC_REG(GICC_EOIR) = iar;
}
