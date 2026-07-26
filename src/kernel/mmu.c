/* src/kernel/mmu.c */
#include <stdint.h>
#include "kernel/mmu.h"

// --- Descriptor Bit Definitions ---
#define DESC_VALID      (1ULL << 0)
#define DESC_TABLE      (1ULL << 1)
#define DESC_BLOCK      (0ULL << 1)
#define DESC_AF         (1ULL << 10)  // Access Flag (Must be 1)
#define DESC_SH_INNER   (3ULL << 8)   // Inner Shareable
#define DESC_ATTR_IDX(x) ((uint64_t)(x) << 2)

// --- Access Permissions (AP) ---
// AP[2:1]: 00 = EL1 RW, 01 = EL1/EL0 RW
#define DESC_AP_KERN_RW (0ULL << 6)
#define DESC_AP_USER_RW (1ULL << 6)

// --- Execute Never (XN) ---
#define DESC_UXN        (1ULL << 54) // Unprivileged (EL0) Execute Never
#define DESC_PXN        (1ULL << 53) // Privileged (EL1) Execute Never

// --- Final Block Attribute Combinations ---
// Kernel RAM: RW for EL1, No Access EL0, EL1 can execute, EL0 cannot.
#define L2_BLOCK_KERNEL (DESC_VALID | DESC_AF | DESC_SH_INNER | DESC_ATTR_IDX(0) | DESC_AP_KERN_RW | DESC_UXN)

// User RAM: RW for EL1/EL0, EL0 can execute.
#define L2_BLOCK_USER   (DESC_VALID | DESC_AF | DESC_SH_INNER | DESC_ATTR_IDX(0) | DESC_AP_USER_RW)

// Device Memory: RW for EL1, No Execute for anyone.
#define L2_BLOCK_DEVICE (DESC_VALID | DESC_AF | DESC_ATTR_IDX(1) | DESC_UXN | DESC_PXN)

// --- System Register Configurations ---
#define TCR_VALUE ((16ULL << 0) | (3ULL << 10) | (3ULL << 12) | (1ULL << 8) | (5ULL << 32))
#define MAIR_VALUE (0xFFULL << 0 | 0x04ULL << 8) // Attr0: Normal, Attr1: Device

static uint64_t l0_table[512] __attribute__((aligned(4096)));
static uint64_t l1_table[512] __attribute__((aligned(4096)));
static uint64_t l2_ram[512]   __attribute__((aligned(4096)));
static uint64_t l2_dev[512]   __attribute__((aligned(4096)));

/* * Flushes data from CPU caches to physical RAM. 
 * Necessary because the MMU walker bypasses the D-Cache.
 */
void clean_cache_range(uint64_t start, uint64_t size) {
    uint64_t line_size = 64; 
    uint64_t end = start + size;
    for (uint64_t addr = (start & ~(line_size - 1)); addr < end; addr += line_size) {
        asm volatile("dc cvac, %0" :: "r"(addr) : "memory");
    }
    asm volatile("dsb sy; isb" ::: "memory");
}

void mmu_init(void) {
    // 1. Reset tables
    for (int i = 0; i < 512; i++) {
        l0_table[i] = 0; l1_table[i] = 0; l2_ram[i] = 0; l2_dev[i] = 0;
    }

    // 2. Build Hierarchy
    l0_table[0] = ((uint64_t)l1_table) | 0x3; // 0-511GB
    l1_table[0] = ((uint64_t)l2_dev)   | 0x3; // 0-1GB (Peripherals)
    l1_table[1] = ((uint64_t)l2_ram)   | 0x3; // 1GB-2GB (RAM at 0x40000000)

    // 3. Map Peripherals (0-1GB range)
    l2_dev[64] = 0x08000000ULL | L2_BLOCK_DEVICE; // GIC Dist
    l2_dev[65] = 0x08200000ULL | L2_BLOCK_DEVICE; // GIC CPU
    l2_dev[72] = 0x09000000ULL | L2_BLOCK_DEVICE; // UART

    // 4. Map RAM (1GB-2GB range)
    // First 2MB block: Kernel Only (Index 0 of l2_ram)
    l2_ram[0] = 0x40000000ULL | L2_BLOCK_KERNEL;

    // Remaining 126MB: User Access allowed (Index 1 to 63)
    // This allows your EL0 task at 0x40200000 to work.
    for (int i = 1; i < 64; i++) {
        l2_ram[i] = (0x40000000ULL + (i * 0x200000ULL)) | L2_BLOCK_USER;
    }

    // 5. Sync to RAM
    clean_cache_range((uint64_t)l0_table, 4096);
    clean_cache_range((uint64_t)l1_table, 4096);
    clean_cache_range((uint64_t)l2_dev, 4096);
    clean_cache_range((uint64_t)l2_ram, 4096);

    // 6. Register Config
    asm volatile("msr mair_el1, %0" :: "r"(MAIR_VALUE));
    asm volatile("msr ttbr0_el1, %0" :: "r"((uint64_t)l0_table));
    asm volatile("msr tcr_el1, %0"   :: "r"(TCR_VALUE));
    asm volatile("isb");

    // 7. Clear TLB
    asm volatile("tlbi vmalle1; dsb sy; isb");

    // 8. Enable MMU and Caches
    uint64_t sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12); 
    asm volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    asm volatile("isb");
}
