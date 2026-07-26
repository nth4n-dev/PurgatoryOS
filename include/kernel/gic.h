/* include/kernel/gic.h */

#ifndef PURGATORY_GIC_H
#define PURGATORY_GIC_H

#include <stdint.h>

/* GICv2 base addresses on QEMU virt */
#define GICD_BASE  0x08000000UL
#define GICC_BASE  0x08010000UL

/* Distributor register offsets */
#define GICD_CTLR       0x000   /* Distributor Control Register    */
#define GICD_ISENABLER  0x100   /* Interrupt Set-Enable Registers  */
#define GICD_IPRIORITYR 0x400   /* Interrupt Priority Registers    */
#define GICD_ITARGETSR  0x800   /* Interrupt Processor Target Regs */
#define GICD_ICFGR      0xC00   /* Interrupt Configuration Regs    */

/* CPU Interface register offsets */
#define GICC_CTLR  0x000   /* CPU Interface Control Register */
#define GICC_PMR   0x004   /* Interrupt Priority Mask Register */
#define GICC_IAR   0x00C   /* Interrupt Acknowledge Register  */
#define GICC_EOIR  0x010   /* End of Interrupt Register       */

/* Helpers */
#define GICD_REG(off) (*(volatile uint32_t *)(GICD_BASE + (off)))
#define GICC_REG(off) (*(volatile uint32_t *)(GICC_BASE + (off)))

void gic_init(void);
void gic_enable_irq(uint32_t irq);
uint32_t gic_acknowledge(void);
void gic_end(uint32_t iar);

#endif //PURGATORY_GIC_H
