/* include/kernel/heap.h
 *
 * Kernel heap allocator. The post builds it in two phases.
 *
 * Phase 1 is a bump allocator. It is fast and it cannot free.
 * Phase 2 is a free-list allocator with real malloc and free semantics.
 *
 * Both phases expose the same kmalloc and kfree interface. Callers do
 * not change when the allocator is upgraded.
 *
 * From Silicon to Shell, Post 8: A Heap Allocator
 */
#ifndef KERNEL_HEAP_H
#define KERNEL_HEAP_H

#include <stddef.h>
#include <stdint.h>

/* Alignment.
 *
 * ALIGN_UP rounds a size up to the nearest multiple of `align`.
 * `align` must be a power of two.
 *
 * Example: ALIGN_UP(13, 8) is 16.
 */
#define ALIGN_UP(n, align)  (((size_t)(n) + (size_t)(align) - 1) \
                             & ~((size_t)(align) - 1))

/* Every pointer kmalloc returns is aligned to this boundary.
 * AArch64 needs 8 bytes for 64-bit stores such as str x0.
 * A misaligned 64-bit store raises a Data Abort (EC=0x25) on real
 * hardware. QEMU forgives it, which is how the bug hides. */
#define HEAP_ALIGN  8

/* Free-list block header, used in phase 2.
 *
 * It sits immediately before every allocation's payload in the heap.
 * The caller never sees it. kmalloc returns payload(header).
 *
 * Memory layout of one block:
 *
 *    ┌─────────────────────────────┐  ← block_header_t *
 *    │  size_t  size               │  payload bytes (not counting header)
 *    │  int     is_free            │  1 = available, 0 = allocated
 *    │  struct* next               │  next block in chain (or NULL)
 *    │  [4 bytes padding]          │  ALIGN_UP(20, 8) = 24 total
 *    ├─────────────────────────────┤  ← void * returned to caller
 *    │                             │
 *    │       payload  (size bytes) │
 *    │                             │
 *    └─────────────────────────────┘
 *
 * HEADER_SIZE is ALIGN_UP(sizeof(block_header_t), HEAP_ALIGN).
 * On AArch64 with -O2 the struct is 20 bytes, so HEADER_SIZE is 24.
 */
typedef struct block_header {
    size_t               size;     /* payload bytes (excludes this header) */
    int                  is_free;  /* 1 = free, 0 = allocated              */
    struct block_header *next;     /* next block in the chain              */
} block_header_t;

#define HEADER_SIZE  ALIGN_UP(sizeof(block_header_t), HEAP_ALIGN)

/* Public API.
 *
 * heap_init  Call this once from kernel_main, before any kmalloc.
 *            It reads __heap_start and __heap_end from the linker script.
 *
 * kmalloc    Allocates `size` bytes aligned to HEAP_ALIGN.
 *            Returns NULL when the heap is full or when size is 0.
 *
 * kfree      Releases a pointer that kmalloc returned.
 *            Passing NULL is safe. Passing anything else is undefined.
 *
 * heap_dump  Prints the allocator state to the UART. Useful when debugging.
 */
void  heap_init(void);
void *kmalloc(size_t size);
void  kfree(void *ptr);
void  heap_dump(void);

#endif /* KERNEL_HEAP_H */
