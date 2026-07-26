/* src/kernel/syscall.c */
#include "kernel/syscall.h"
#include "kernel/scheduler.h"
#include "drivers/uart.h"


/* Imported from libpurgatory_rs.a */
extern uint32_t fs_open(const char *path, uint64_t len);
extern int64_t  fs_read(uint32_t id, uint64_t off, char *dst, uint64_t len);
extern int64_t  fs_close(uint32_t id);

/* Saved register frame layout. Must match SAVE_REGS in vectors.S.
 * Registers are pushed in pairs, lowest index at lowest address. */
typedef struct regs {
    /* Registers are pushed in reverse order by SAVE_REGS.
     * x30 is pushed last, so it is at the lowest address (top of stack). */
    uint64_t x30;
    uint64_t x28, x29;
    uint64_t x26, x27;
    uint64_t x24, x25;
    uint64_t x22, x23;
    uint64_t x20, x21;
    uint64_t x18, x19;
    uint64_t x16, x17;
    uint64_t x14, x15;
    uint64_t x12, x13;
    uint64_t x10, x11;
    uint64_t x8,  x9;
    uint64_t x6,  x7;
    uint64_t x4,  x5;
    uint64_t x2,  x3;
    uint64_t x0,  x1; // x0 and x1 were pushed first, so they are at the highest address
} regs_t;

/* Exception class field in ESR_EL1, bits [31:26]. */
#define ESR_EC_SHIFT   26
#define ESR_EC_MASK    0x3F
#define EC_SVC64       0x15

/* copy_from_user: bring `len` bytes from the user-space `src` into the
 * kernel-allocated `dst`. In Post 12 we trust EL0 pointers to be valid
 *. Post 13's MMU work hardens this with access_ok(). */
static int64_t copy_from_user(char *dst, const char *src, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) dst[i] = src[i];
    return (int64_t)len;
}

static int64_t copy_to_user(char *dst, const char *src, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) dst[i] = src[i];
    return (int64_t)len;
}

#define PATH_MAX 128

static int64_t k_sys_open(const char *user_path, uint64_t len) {
    if (len == 0 || len >= PATH_MAX)       return ENAMETOOLONG;
    if (user_path == NULL)                 return EINVAL;

    /* Copy the path into a kernel buffer before calling Rust. We do
     * not want a user-space pointer held by the Rust allocator lock. */
    char path[PATH_MAX];
    copy_from_user(path, user_path, len);
    path[len] = '\0';

    uint32_t id = fs_open(path, len);
    if (id == 0) return ENOENT;

    /* Allocate a free slot in the current task's fd table. fd 0/1/2 are
     * reserved for stdin/stdout/stderr even though we don't use them yet. */
    pcb_t *me = current_task_pcb();
    for (int fd = 3; fd < MAX_OPEN_FILES; fd++) {
        if (me->fds[fd].node_id == 0) {
            me->fds[fd].node_id = id;
            me->fds[fd].offset  = 0;
            return fd;
        }
    }
    return EMFILE;
}

static int64_t k_sys_read(int fd, char *user_buf, uint64_t len) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return EBADF;
    if (user_buf == NULL || len == 0)   return 0;

    pcb_t *me = current_task_pcb();
    fd_entry_t *slot = &me->fds[fd];
    if (slot->node_id == 0) return EBADF;

    /* Again: bounce through a kernel buffer. Keeps the Rust side from
     * ever touching EL0-writable memory. 256 B is a generous chunk for
     * Post 12's demo files; a real read(2) would loop. */
    char kbuf[256];
    uint64_t chunk = len > sizeof(kbuf) ? sizeof(kbuf) : len;

    int64_t n = fs_read(slot->node_id, slot->offset, kbuf, chunk);
    if (n < 0) return n;

    copy_to_user(user_buf, kbuf, (uint64_t)n);
    slot->offset += (uint64_t)n;
    return n;
}

static int64_t k_sys_close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return EBADF;

    pcb_t *me = current_task_pcb();
    if (me->fds[fd].node_id == 0) return EBADF;

    fs_close(me->fds[fd].node_id);
    me->fds[fd].node_id = 0;
    me->fds[fd].offset  = 0;
    return 0;
}

void el0_sync_handler(uint64_t esr, regs_t *r) {
    uint32_t ec = (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;

    if (ec == EC_SVC64) {
        /* Write the return value into the saved x0 slot. The eret
         * that follows will restore it into the user's x0. */
        r->x0 = syscall_dispatch(r);
        return;
    }

    /* Anything else from EL0 is, for now, fatal to the task:
     * unaligned access, MMU fault in user space, undefined instruction.
     * We kill the task instead of taking down the whole kernel. */
    kprint("[el0] unhandled sync, ESR=0x");
    /* Print ESR as hex so the exception class (bits [31:26]) is visible. */
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (esr >> shift) & 0xF;
        uart_putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    kprint("\n");
    task_kill_current();
}

/* Forward declarations for the kernel-side handlers below. */
static int64_t k_sys_write(int fd, const char *buf, uint64_t len);
static int64_t k_sys_exit(int code);
static int64_t k_sys_getpid(void);
static int64_t k_sys_yield(void);

/* The table is indexed by syscall number.  NULL entries are illegal .
 * we explicitly bounds-check before indexing.  An entry is a plain
 * function pointer that takes up to 6 uint64_t args; the individual
 * handlers cast them to their real types. */
typedef int64_t (*syscall_fn)(uint64_t, uint64_t, uint64_t,
                              uint64_t, uint64_t, uint64_t);

static syscall_fn syscall_table[SYS_MAX] = {
    [SYS_WRITE]  = (syscall_fn)k_sys_write,
    [SYS_EXIT]   = (syscall_fn)k_sys_exit,
    [SYS_GETPID] = (syscall_fn)k_sys_getpid,
    [SYS_YIELD]  = (syscall_fn)k_sys_yield,
    [SYS_OPEN]   = (syscall_fn)k_sys_open,
    [SYS_READ]   = (syscall_fn)k_sys_read,
    [SYS_CLOSE]  = (syscall_fn)k_sys_close,
};

/* syscall_init. Called once from kernel_main before any EL0 code runs.
 * The table is a static C initializer so there is nothing runtime-wise to
 * populate; the job of this function is to sanity-check the table and
 * announce readiness on the UART.  It mirrors the style of scheduler_init,
 * heap_init, etc.. One line of setup diagnostics per subsystem. */
void syscall_init(void) {
    /* Bounds check: every non-zero slot below SYS_MAX must point somewhere.
     * Catches the "added an enum entry but forgot to wire the table row"
     * bug at boot instead of at the first svc. */
    for (uint64_t nr = 1; nr < SYS_MAX; nr++) {
        if (syscall_table[nr] == NULL) {
            kprint("[sys] FATAL: syscall_table slot missing\n");
            while (1) { asm volatile("wfi"); }
        }
    }
    kprint("[sys] init: 4 syscalls registered (write/exit/getpid/yield)\n");
}

int64_t syscall_dispatch(regs_t *r) {
    uint64_t nr = r->x8;

    if (nr >= SYS_MAX || syscall_table[nr] == NULL) {
        kprint("[sys] ENOSYS: nr=");
        /* ... print nr, then return -ENOSYS ... */
        return ENOSYS;
    }

    /* Call the handler with up to six arguments from the saved frame.
     * Handlers that take fewer arguments simply ignore the extra ones;
     * AAPCS64 allows this. */
    return syscall_table[nr](r->x0, r->x1, r->x2, r->x3, r->x4, r->x5);
}

static int64_t k_sys_write(int fd, const char *buf, uint64_t len) {
    /* fd is ignored for now. We only have stdout, which is the UART.
     * A real kernel would look fd up in a per-process file descriptor
     * table and dispatch to the appropriate driver. */
    if (fd != 1) return -EBADF;
    if (buf == NULL) return -EINVAL;

    /* NOTE: no validation of `buf`.  A malicious EL0 task could pass a
     * pointer into kernel memory and have the kernel "write" it out via
     * the UART, leaking secrets.  Post 11 (Rust) and Post 12 (filesystem)
     * are where we introduce access_ok() checks against the user's page
     * table permissions.  For now, we trust EL0.  Documented sin. */
    for (uint64_t i = 0; i < len; i++) uart_putc(buf[i]);
    return (int64_t)len;
}

static int64_t k_sys_exit(int code) {
    (void)code; /* no exit status plumbing yet */
    task_kill_current();
    /* task_kill_current calls schedule_next, which never returns here. */
    return 0;
}

static int64_t k_sys_getpid(void) {
    return (int64_t)current_task_pid();
}

static int64_t k_sys_yield(void) {
    scheduler_yield();
    return 0;
}
