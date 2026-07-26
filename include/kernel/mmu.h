/* include/kernel/mmu.h
 *
 * Memory Management Unit initialisation.
 *
 * Builds a three-level AArch64 page table (L0, L1, L2) with an identity
 * map. Every virtual address equals its physical address.
 *
 * The first gigabyte holds peripherals, so it maps through an L2 table
 * of Device-nGnRnE blocks. That covers the PL011 UART at 0x09000000.
 * Kernel RAM at 0x40000000 maps as Normal write-back cacheable.
 *
 * mmu_init writes MAIR_EL1 and TCR_EL1, then sets SCTLR_EL1.M to bring
 * the MMU online.
 *
 * From Silicon to Shell, Post 6: Virtual Memory and the MMU
 */
#ifndef KERNEL_MMU_H
#define KERNEL_MMU_H

void mmu_init(void);

#endif /* KERNEL_MMU_H */
