/* include/kernel/syscall.h */
#ifndef PURGATORY_SYSCALL_H
#define PURGATORY_SYSCALL_H

#include <stdint.h>

/* Syscall numbers.  These are our ABI. Once a user program is compiled
 * against them, we can't change the numbers without breaking it. */
typedef enum {
    SYS_WRITE  = 1,
    SYS_EXIT   = 2,
    SYS_GETPID = 3,
    SYS_YIELD  = 4,
    SYS_MAX
} syscall_nr_t;

/* Error codes. Follow the Linux convention of negative errno. */
#define ENOSYS   -38    /* unknown syscall */
#define EINVAL   -22    /* bad argument */
#define EBADF    -9     /* bad file descriptor */

typedef struct regs regs_t; /* defined in syscall.c */

void syscall_init(void);

int64_t syscall_dispatch(regs_t *r);

int64_t sys_write(int fd, const char *buf, uint64_t len);

int64_t sys_exit(int code);

int64_t sys_getpid(void);

int64_t sys_yield(void);

#endif //PURGATORY_SYSCALL_H
