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

/*
 * task_entry_trampoline: first-run stub for EL0 tasks.
 *
 * context_switch restores the task's kernel registers (all zeroed for a new
 * task) and does `ret`, landing here.  We load ELR_EL1/SPSR_EL1/SP_EL0 from
 * the current task's uctx and eret into user space.
 * Never called a second time: subsequent context_switch calls restore the
 * return address inside schedule_next, unwinding normally through irq/sync
 * handlers back to eret.
 */
static void __attribute__((noreturn)) task_entry_trampoline(void) {
    pcb_t *pcb = tasks[current_task];
    asm volatile(
        "msr sp_el0,   %0\n"
        "msr elr_el1,  %1\n"
        "msr spsr_el1, %2\n"
        "mov x0,  xzr\n"
        "mov x1,  xzr\n"
        "mov x2,  xzr\n"
        "eret"
        :
        : "r"(pcb->uctx.sp_el0), "r"(pcb->uctx.elr_el1), "r"(pcb->uctx.spsr_el1)
    );
    __builtin_unreachable();
}

pcb_t *task_create(void (*entry)(void)) {
    pcb_t   *pcb    = kmalloc(sizeof(pcb_t));
    uint8_t *kstack = kmalloc(TASK_STACK_SIZE);
    uint8_t *ustack = kmalloc(USER_STACK_SIZE);
    /* ... NULL checks omitted for brevity ... */

    zero_memory(pcb, sizeof(pcb_t));
    pcb->pid         = task_count;
    pcb->state       = TASK_READY;
    pcb->entry       = entry;
    pcb->kstack_base = kstack;
    pcb->ustack_base = ustack;
    pcb->kstack_size = TASK_STACK_SIZE;
    pcb->ustack_size = USER_STACK_SIZE;

    /* User-side state. Consumed by first_eret_to_el0 on scheduler_start. */
    pcb->uctx.sp_el0   = ((uint64_t)(ustack + USER_STACK_SIZE)) & ~0xFUL;
    pcb->uctx.elr_el1  = (uint64_t)entry;
    pcb->uctx.spsr_el1 = 0;           /* EL0t, all exceptions unmasked */

    /* Kernel-side state. Used when the timer preempts the task at EL0
     * and we need to context_switch to another task.  The Post 9 trick
     * still works: set ctx.sp to the top of the kernel stack, and
     * ctx.lr to the point in schedule_next where context_switch returns. */
    pcb->ctx.sp = ((uint64_t)(kstack + TASK_STACK_SIZE)) & ~0xFUL;
    pcb->ctx.lr = (uint64_t)task_entry_trampoline;

    tasks[task_count++] = pcb;
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

void task_kill_current(void) {
    tasks[current_task]->state = TASK_DEAD;
    schedule_next();
    /* schedule_next returns only if all other tasks are also dead. */
    while (1) { asm volatile("wfi"); }
}

uint32_t current_task_pid(void) {
    return tasks[current_task]->pid;
}

pcb_t *current_task_pcb(void) {
    return tasks[current_task];
}
