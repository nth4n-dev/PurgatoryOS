/* include/kernel/mmu.h
 *
 * Memory Management Unit initialisation.
 *
 * Sets up a three-level AArch64 page table (L0 → L1 → L2) with an
 * identity map (VA = PA), enables MAIR_EL1, TCR_EL1, and then flips
 * SCTLR_EL1.M to bring the MMU online.
 *
 * From Silicon to Shell. Post 6: Virtual Memory & the MMU
 */
#ifndef KERNEL_MMU_H
#define KERNEL_MMU_H

#define DESC_AP_RW_ALL  (1UL << 6)

/* Update your User Block definition */
#define BLOCK_NORMAL_USER \
(DESC_VALID | DESC_BLOCK | DESC_AF | \
DESC_SH_INNER | DESC_AP_RW_ALL | DESC_ATTR(0))

void mmu_init(void);

#endif /* KERNEL_MMU_H */
