/* kernel/heap.c: the free-list heap allocator.
 *
 * The strategy is first fit, with forward coalescing on free.
 *
 * kmalloc walks the block chain from heap_head. It takes the first free
 * block whose payload is large enough for the aligned request. If the
 * leftover space can hold a header plus HEAP_ALIGN bytes, it splits the
 * block. Then it marks the block allocated and returns the payload.
 *
 * kfree recovers the header by subtracting HEADER_SIZE from the payload
 * pointer, then sets is_free. If the next block is also free, the two
 * merge into one. That is forward coalescing. Backward coalescing needs
 * a footer tag or a doubly-linked list. It is left as an exercise.
 *
 * Allocation is O(n) and free is O(1), plus one forward scan to coalesce.
 * That is fine for a kernel with a few thousand live allocations.
 *
 * From Silicon to Shell, Post 8: A Heap Allocator
 */

#include "kernel/heap.h"
#include "drivers/uart.h"
#include <stdint.h>

/* Linker script symbols.
 *
 * These are declared as char arrays so the compiler treats the symbol
 * name as an address, not as a variable to dereference.
 * They mark the 1 MB heap reserved in link.ld:
 *
 *   __heap_start = .;
 *   . += 0x100000;
 *   __heap_end   = .;
 */
extern char __heap_start[];
extern char __heap_end[];

/* Module state */

static block_header_t *heap_head;   /* first block in the linked chain */

/* Private helpers */

static block_header_t *write_header(char *addr, size_t size,
                                    int is_free, block_header_t *next)
{
    block_header_t *hdr = (block_header_t *)addr;
    hdr->size    = size;
    hdr->is_free = is_free;
    hdr->next    = next;
    return hdr;
}

/* payload: pointer to the bytes just after the header */
static inline void *payload(block_header_t *hdr) {
    return (char *)hdr + HEADER_SIZE;
}

/* header: recover the header from a payload pointer */
static inline block_header_t *header_from_payload(void *ptr) {
    return (block_header_t *)((char *)ptr - HEADER_SIZE);
}

/* Minimal decimal printer (no printf available) */

static void print_size(size_t n) {
    char buf[24];
    int pos = sizeof(buf) - 1;
    buf[pos] = '\0';
    if (n == 0) { buf[--pos] = '0'; }
    while (n > 0) { buf[--pos] = '0' + (int)(n % 10); n /= 10; }
    kprint(&buf[pos]);
}

static void print_ptr(const void *p) {
    uintptr_t val = (uintptr_t)p;
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        int nibble = (int)(val & 0xF);
        buf[2 + i] = nibble < 10 ? (char)('0' + nibble)
                                 : (char)('a' + nibble - 10);
        val >>= 4;
    }
    buf[18] = '\0';
    kprint(buf);
}

/* Public API */

void heap_init(void) {
    size_t total        = (size_t)(__heap_end - __heap_start);
    size_t payload_size = total - HEADER_SIZE;

    heap_head = write_header(__heap_start, payload_size, /*is_free=*/1, NULL);

    kprint("[heap] init  start=");
    print_ptr(__heap_start);
    kprint(" end=");
    print_ptr(__heap_end);
    kprint(" total=");
    print_size(total);
    kprint(" B\n");
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Round up to satisfy AArch64 alignment requirement. */
    size = ALIGN_UP(size, HEAP_ALIGN);

    block_header_t *cur = heap_head;
    while (cur != NULL) {
        if (cur->is_free && cur->size >= size) {

            size_t remainder = cur->size - size;

            /*
             * Split the block only if the remainder can hold a full
             * header plus at least one HEAP_ALIGN-byte payload.
             * Without this guard, the remainder can underflow (size_t
             * is unsigned) and produce a corrupt header address.
             */
            if (remainder > HEADER_SIZE + HEAP_ALIGN) {
                char *split_addr = (char *)payload(cur) + size;
                block_header_t *split = write_header(
                    split_addr,
                    remainder - HEADER_SIZE,
                    /*is_free=*/1,
                    cur->next
                );
                cur->size = size;
                cur->next = split;
            }

            cur->is_free = 0;
            return payload(cur);
        }
        cur = cur->next;
    }

    kprint("[heap] OOM: kmalloc(");
    print_size(size);
    kprint(") returning NULL\n");
    return NULL;
}

void kfree(void *ptr) {
    if (ptr == NULL) return;

    block_header_t *hdr = header_from_payload(ptr);
    hdr->is_free = 1;

    /* Forward coalescing: merge with the next block if it is also free. */
    if (hdr->next != NULL && hdr->next->is_free) {
        hdr->size += HEADER_SIZE + hdr->next->size;
        hdr->next  = hdr->next->next;
    }
}

void heap_dump(void) {
    kprint("[heap] --- free-list dump ---\n");
    block_header_t *cur = heap_head;
    int idx = 0;
    while (cur != NULL) {
        kprint("[heap] block ");
        print_size((size_t)idx);
        kprint(" @ ");
        print_ptr(cur);
        kprint(": size=");
        print_size(cur->size);
        kprint(cur->is_free ? " FREE\n" : " USED\n");
        cur = cur->next;
        idx++;
    }
    kprint("[heap] -------------------\n");
}
