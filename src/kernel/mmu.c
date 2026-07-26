/* kernel/mmu.c */
#include <stdint.h>
#include "kernel/mmu.h"

/* --- Descriptor bit definitions --- */
#define DESC_VALID      (1UL << 0)
#define DESC_TABLE      (1UL << 1)   /* Level 0, 1, 2 pointing to next table */
#define DESC_BLOCK      (0UL << 1)   /* Level 1, 2 terminal blocks */

#define DESC_AF         (1UL << 10)
#define DESC_SH_INNER   (3UL << 8)
#define DESC_SH_NONE    (0UL << 8)
#define DESC_AP_RW_EL1  (0UL << 6)
#define DESC_ATTR(idx)  ((uint64_t)(idx) << 2)
#define DESC_PXN        (1UL << 53)
#define DESC_UXN        (1UL << 54)

/* --- Composite block descriptors --- */
#define BLOCK_NORMAL \
    (DESC_VALID | DESC_BLOCK | DESC_AF | \
     DESC_SH_INNER | DESC_AP_RW_EL1 | DESC_ATTR(0))

#define BLOCK_DEVICE \
    (DESC_VALID | DESC_BLOCK | DESC_AF | \
     DESC_SH_NONE | DESC_AP_RW_EL1 | DESC_ATTR(1) | DESC_PXN | DESC_UXN)

/* --- MAIR & TCR Settings --- */
#define MAIR_NORMAL_WB  0xFFUL
#define MAIR_DEVICE_NG  0x00UL
#define MAIR_VALUE      ((MAIR_DEVICE_NG << 8) | MAIR_NORMAL_WB)

#define TCR_T0SZ        (16UL << 0)
#define TCR_IRGN0_WB    (1UL  << 8)
#define TCR_ORGN0_WB    (1UL  << 10)
#define TCR_SH0_INNER   (3UL  << 12)
#define TCR_TG0_4K      (0UL  << 14)
#define TCR_EPD1        (1UL  << 23)
#define TCR_IPS_48BIT   (5UL  << 32)
#define TCR_VALUE       (TCR_T0SZ | TCR_IRGN0_WB | TCR_ORGN0_WB | \
                         TCR_SH0_INNER | TCR_TG0_4K | TCR_EPD1 | TCR_IPS_48BIT)

/* --- Page tables --- */
static uint64_t l0_table[512]     __attribute__((aligned(4096)));
static uint64_t l1_table[512]     __attribute__((aligned(4096)));
static uint64_t l2_dev_table[512] __attribute__((aligned(4096)));
static uint64_t l2_table[512]     __attribute__((aligned(4096)));

/*
 * Clean the data cache by VA to the Point of Coherency.
 * The MMU table walker reads from RAM, not from the data cache.
 * Without this it sees stale zeros and every translation faults.
 */
void clean_cache_range(uint64_t start, uint64_t size) {
    uint64_t line_size = 64;
    uint64_t end = start + size;
    start &= ~(line_size - 1); /* align down to a cache line */

    for (uint64_t addr = start; addr < end; addr += line_size) {
        asm volatile("dc cvac, %0" :: "r"(addr) : "memory");
    }
    asm volatile("dsb sy" ::: "memory");
}

void mmu_init(void) {
    /* 1. Zero the tables. The boot stub should have cleared .bss already.
     * One stale non-zero entry would translate to an arbitrary address. */
    for (int i = 0; i < 512; i++) {
        l0_table[i]     = 0;
        l1_table[i]     = 0;
        l2_dev_table[i] = 0;
        l2_table[i]     = 0;
    }

    /* 2. Build the L0 table. One entry covers the first 512 GB. */
    l0_table[0] = (uint64_t)l1_table | DESC_VALID | DESC_TABLE;

    /* 3. Build the L1 table.
     * L1[0] covers 0 GB to 1 GB and points at the device L2 table.
     * L1[1] covers 1 GB to 2 GB and points at the kernel L2 table. */
    l1_table[0] = (uint64_t)l2_dev_table | DESC_VALID | DESC_TABLE;
    l1_table[1] = (uint64_t)l2_table | DESC_VALID | DESC_TABLE;

    /* 4. Build the device L2 table.
     * 512 blocks of 2 MB cover 0x00000000 to 0x3FFFFFFF.
     * On QEMU virt that whole gigabyte is memory-mapped I/O. It holds
     * flash, the GIC, the PL011 UART at 0x09000000 (entry 72), the RTC,
     * the virtio-mmio transports and the PCIe ECAM window.
     * Device attributes stop the CPU caching, merging or reordering
     * these accesses. The volatile keyword in the UART driver only
     * constrains the compiler. The page tables constrain the CPU. */
    for (int i = 0; i < 512; i++) {
        uint64_t pa = (uint64_t)i * 0x200000UL;
        l2_dev_table[i] = pa | BLOCK_DEVICE;
    }

    /* 5. Build the kernel L2 table.
     * Maps 16 MB from 0x40000000, the base of RAM on QEMU virt. */
    for (int i = 0; i < 8; i++) {
        uint64_t pa = 0x40000000UL + (uint64_t)i * 0x200000UL;
        l2_table[i] = pa | BLOCK_NORMAL;
    }

    /* 6. Clean the tables to RAM so the hardware walker can see them.
     * Skip this and the MMU faults the moment you enable it. */
    clean_cache_range((uint64_t)l0_table,     4096);
    clean_cache_range((uint64_t)l1_table,     4096);
    clean_cache_range((uint64_t)l2_dev_table, 4096);
    clean_cache_range((uint64_t)l2_table,     4096);

    /* 7. Program System Registers */
    asm volatile("msr mair_el1,  %0" :: "r"(MAIR_VALUE) : "memory");
    asm volatile("msr tcr_el1,   %0" :: "r"(TCR_VALUE)  : "memory");
    asm volatile("msr ttbr0_el1, %0" :: "r"((uint64_t)l0_table) : "memory");
    asm volatile("isb");

    /* 8. Invalidate TLBs and Instruction Cache */
    asm volatile("tlbi vmalle1" ::: "memory");
    asm volatile("ic iallu"     ::: "memory"); /* invalidate the icache */
    asm volatile("dsb sy"       ::: "memory");
    asm volatile("isb");

    /* 9. Enable MMU and Caches */
    uint64_t sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1UL << 0);   /* M - MMU */
    sctlr |= (1UL << 2);   /* C - Data Cache */
    sctlr |= (1UL << 12);  /* I - Instruction Cache */
    asm volatile("msr sctlr_el1, %0" :: "r"(sctlr) : "memory");
    asm volatile("isb");
}
