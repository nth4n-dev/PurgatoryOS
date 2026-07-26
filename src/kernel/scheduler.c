/* kernel/scheduler.c
 *
 * Round-robin task scheduler. It runs cooperatively and preemptively.
 *
 * From Silicon to Shell, Post 9: Processes and Context Switching
 *
 * Tasks are stored in a fixed-size circular array.  schedule_next() advances
 * a cursor through the array and calls context_switch() to perform the actual
 * register swap.  The CPU's perspective: context_switch() is a function call
 * that "returns" inside a different task's stack.
 */

#include "kernel/scheduler.h"
#include "kernel/heap.h"
#include "drivers/uart.h"
#include <stdint.h>

/* Forward declaration. Defined in arch/arm64/context_switch.S */
extern void context_switch(cpu_context_t *old, cpu_context_t *new_ctx);
extern void start_first_task(cpu_context_t *new_ctx);

/* Internal state */

static pcb_t *tasks[MAX_TASKS];   /* runqueue. NULL slots are empty    */
static int    task_count   = 0;   /* number of live tasks               */
static int    current_task = 0;   /* index of the currently running task */

/* Private helpers */

/* Minimal decimal printer. We have no printf. */
static void kprint_uint(uint32_t n) {
    char buf[12];
    int pos = 11;
    buf[pos] = '\0';
    if (n == 0) {
        buf[--pos] = '0';
    } else {
        while (n > 0) {
            buf[--pos] = '0' + (n % 10);
            n /= 10;
        }
    }
    kprint(&buf[pos]);
}

/* Zero a region of memory (we have no memset from libc). */
static void zero_memory(void *ptr, size_t size) {
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < size; i++) p[i] = 0;
}

/*
 * schedule_next: select the next ready task and switch to it.
 *
 * Advances current_task by one in round-robin order, skipping DEAD tasks.
 * If there is only one task, or no other ready task, returns immediately.
 * Called by both scheduler_yield() and scheduler_tick().
 */
static void schedule_next(void) {
    if (task_count <= 1) return;

    int old_idx = current_task;
    int new_idx = (old_idx + 1) % task_count;

    /* Skip dead tasks.  In practice, for Post 9, all tasks run forever. */
    int scanned = 0;
    while (tasks[new_idx]->state == TASK_DEAD) {
        new_idx = (new_idx + 1) % task_count;
        if (++scanned >= task_count) return; /* all other tasks are dead */
    }

    if (new_idx == old_idx) return; /* only one runnable task */

    pcb_t *old = tasks[old_idx];
    pcb_t *new = tasks[new_idx];

    old->state   = TASK_READY;
    new->state   = TASK_RUNNING;
    current_task = new_idx;

    /* The magic line.  From old's perspective, context_switch is a normal
     * function call that returns when the scheduler selects old again.
     * From new's perspective, context_switch "returns" and execution resumes
     * at the saved lr. That is either inside scheduler_yield (if new yielded before)
     * or at new->entry (if this is new's first run). */
    context_switch(&old->ctx, &new->ctx);
}

/* Public API */

void scheduler_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) tasks[i] = NULL;
    task_count   = 0;
    current_task = 0;
    kprint("[sched] init: round-robin scheduler\n");
}

pcb_t *task_create(void (*entry)(void)) {
    if (task_count >= MAX_TASKS) {
        kprint("[sched] task_create: task table full\n");
        return NULL;
    }

    /* Allocate the PCB from the kernel heap */
    pcb_t *pcb = kmalloc(sizeof(pcb_t));
    if (!pcb) {
        kprint("[sched] task_create: heap full (PCB)\n");
        return NULL;
    }

    /* Allocate a dedicated stack */
    uint8_t *stack = kmalloc(TASK_STACK_SIZE);
    if (!stack) {
        kfree(pcb);
        kprint("[sched] task_create: heap full (stack)\n");
        return NULL;
    }

    /* Clear PCB to zero so all fields default to 0/NULL. */
    zero_memory(pcb, sizeof(pcb_t));

    /* Populate the PCB */
    pcb->pid        = (uint32_t)task_count;
    pcb->state      = TASK_READY;
    pcb->entry      = entry;
    pcb->stack_base = stack;
    pcb->stack_size = TASK_STACK_SIZE;

    /* Set up the initial cpu_context_t
     *
     * AArch64 stacks grow downward.  The top of the allocated block is
     * stack + TASK_STACK_SIZE.  We start the stack pointer there.
     *
     * The critical trick: we set ctx.lr = entry.  When context_switch
     * first runs for this task, it loads lr from ctx, then executes `ret`.
     * `ret` jumps to the value in lr, which is entry(). So the task's
     * first ret is equivalent to a call to entry().
     *
     * All other callee-saved registers are left as 0.  The C compiler
     * doesn't assume anything about their values at function entry, so
     * this is safe. */
    uint64_t stack_top = (uint64_t)(stack + TASK_STACK_SIZE);

    /* AArch64 requires the stack pointer to be 16-byte aligned.
     * TASK_STACK_SIZE is a multiple of 16, so stack_top is already aligned
     * if stack (from kmalloc) is 8-byte aligned.  Mask the low bits to be sure. */
    stack_top = stack_top & ~(uint64_t)0xF;

    pcb->ctx.sp  = stack_top;
    pcb->ctx.lr  = (uint64_t)entry;
    pcb->ctx.fp  = 0;
    /* x19 to x28 are already 0 from zero_memory. */

    /* Register the task */
    tasks[task_count++] = pcb;

    kprint("[sched] task created: pid=");
    kprint_uint(pcb->pid);
    kprint(" entry=0x");
    kprint("\n");

    return pcb;
}

void scheduler_start(void) {
    if (task_count == 0) {
        kprint("[sched] scheduler_start: no tasks\n");
        return;
    }

    pcb_t *first = tasks[0];
    first->state   = TASK_RUNNING;
    current_task   = 0;

    kprint("[sched] starting first task (pid=0)\n");

    /* Jump into the first task.  We abandon the kernel_main stack here.
     * start_first_task is the restore-only half of context_switch. */
    start_first_task(&first->ctx);

    /* Unreachable. Start_first_task never returns. */
    while (1) { asm volatile("wfi"); }
}

void scheduler_yield(void) {
    schedule_next();
}

void scheduler_tick(void) {
    /* Called from the timer IRQ handler in kernel/exceptions.c.
     * The IRQ prologue in vectors.S has already saved all caller-saved
     * registers on the stack.  All we need to do is pick the next task. */
    schedule_next();
}
