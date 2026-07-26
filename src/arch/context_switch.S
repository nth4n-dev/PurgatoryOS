/* arch/arm64/context_switch.S
 *
 * context_switch(cpu_context_t *old, cpu_context_t *new_ctx)
 * start_first_task(cpu_context_t *new_ctx)
 *
 * From Silicon to Shell, Post 9: Processes and Context Switching
 *
 * The single most important thing to understand before reading this file:
 *
 *   A context switch is just a function call that never returns to the same task.
 *
 * We call context_switch(old, new) from inside scheduler_yield() or
 * scheduler_tick().  context_switch saves the callee-saved registers of the
 * *current* (old) task to old->cpu_context_t, then loads the callee-saved
 * registers of the *next* (new) task from new->cpu_context_t, switches the
 * stack pointer, and executes `ret`.
 *
 * `ret` jumps to the value in x30 (the link register).  For a task that has
 * yielded before, that value was saved when it last called context_switch.
 * It is the return address inside scheduler_yield, so the task resumes exactly
 * where it left off.  For a brand-new task, that value was set by task_create
 * to the task's entry function. So the first ret to that task calls entry().
 *
 * Why only callee-saved registers (x19 to x28, fp, lr, sp)?
 * See the comment in include/kernel/scheduler.h.
 *
 * cpu_context_t field offsets (must match the struct in scheduler.h):
 *   x19 @ +0   x20 @ +8   x21 @ +16  x22 @ +24
 *   x23 @ +32  x24 @ +40  x25 @ +48  x26 @ +56
 *   x27 @ +64  x28 @ +72
 *   fp  @ +80  (x29. Frame pointer)
 *   lr  @ +88  (x30. Return address)
 *   sp  @ +96
 */

.section .text
.global context_switch
.type   context_switch, %function

/* context_switch
 * void context_switch(cpu_context_t *old, cpu_context_t *new_ctx)
 *   x0 = old  (save registers here)
 *   x1 = new  (restore registers from here)
 */
context_switch:

    /* Save old task's callee-saved registers */
    stp  x19, x20, [x0, #0]
    stp  x21, x22, [x0, #16]
    stp  x23, x24, [x0, #32]
    stp  x25, x26, [x0, #48]
    stp  x27, x28, [x0, #64]
    stp  x29, x30, [x0, #80]    /* fp (x29) and lr (x30) */
    mov  x2, sp
    str  x2,       [x0, #96]    /* sp. Can't use stp for sp directly */

    /* Restore new task's callee-saved registers */
    ldp  x19, x20, [x1, #0]
    ldp  x21, x22, [x1, #16]
    ldp  x23, x24, [x1, #32]
    ldp  x25, x26, [x1, #48]
    ldp  x27, x28, [x1, #64]
    ldp  x29, x30, [x1, #80]    /* fp and lr */
    ldr  x2,       [x1, #96]
    mov  sp, x2                  /* switch to new task's stack */

    ret                          /* jump to new task's saved lr */


/* start_first_task
 * void start_first_task(cpu_context_t *new_ctx)
 *   x0 = new  (restore registers from here)
 *
 * Used once by scheduler_start() to switch into the very first task.
 * There is no "old" task to save into. We're leaving the kernel_main
 * context behind permanently.  This is just the restore half of context_switch.
 */
.global start_first_task
.type   start_first_task, %function

start_first_task:
    ldp  x19, x20, [x0, #0]
    ldp  x21, x22, [x0, #16]
    ldp  x23, x24, [x0, #32]
    ldp  x25, x26, [x0, #48]
    ldp  x27, x28, [x0, #64]
    ldp  x29, x30, [x0, #80]
    ldr  x2,       [x0, #96]
    mov  sp, x2
    ret
