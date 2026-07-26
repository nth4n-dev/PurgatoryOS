/* include/kernel/scheduler.h
 *
 * Round-robin task scheduler. It runs cooperatively and preemptively.
 *
 * From Silicon to Shell, Post 9: Processes and Context Switching
 *
 * Design overview.
 *
 * A task is the kernel's unit of concurrent execution. Each task owns:
 *   - A PCB (process control block) allocated from the kernel heap.
 *   - A dedicated kernel stack, also heap-allocated.
 *   - A cpu_context_t that holds its saved register state whenever it is
 *     not the running task.
 *
 * The scheduler maintains a fixed-size circular array of PCB pointers and
 * advances through it in round-robin order on every call to schedule_next().
 * schedule_next() is called by both scheduler_yield() (cooperative) and
 * scheduler_tick() (preemptive, from the timer IRQ).
 */

#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <stdint.h>
#include <stddef.h>

/* Configuration */

/* Stack size per task in bytes.
 * 8 KB gives roughly 500 to 1000 nested call frames. Generous for our tasks. */
#define TASK_STACK_SIZE  (8 * 1024)

/* Maximum number of concurrently live tasks.
 * Keep small: the scheduler array is O(1) per switch but O(n) per look-up. */
#define MAX_TASKS  8

/* Task state */

typedef enum {
    TASK_READY,     /* in the runqueue, waiting for CPU time        */
    TASK_RUNNING,   /* currently executing on the CPU               */
    TASK_BLOCKED,   /* waiting for an event (future work)           */
    TASK_DEAD,      /* finished; slot may be reclaimed              */
} task_state_t;

/* Saved CPU context */

/*
 * cpu_context_t: the minimum register set needed to resume a task.
 *
 * Why only callee-saved registers (x19 to x28 / fp / lr) plus sp?
 *
 * A context switch in our kernel is triggered by a C function call:
 * the task calls scheduler_yield(), which calls schedule_next(), which
 * calls context_switch(old, new).  The C ABI (AAPCS64) defines two classes
 * of general-purpose registers:
 *
 *   Caller-saved (x0 to x18): the *caller* is responsible for preserving these
 *     across any function call if it cares about their values.  The compiler
 *     assumes they may be clobbered on return.  From the perspective of the
 *     code that called scheduler_yield(), those registers are already dead.
 *     Either the compiler saved them before the call, or it did not need
 *     them. Either way we do not preserve them.
 *
 *   Callee-saved (x19 to x28, fp, lr): the *callee* must preserve these.
 *     When scheduler_yield() returns to the task, the task's compiler-generated
 *     code assumes these still hold the values they had before the call.
 *     So we must save and restore them.
 *
 *   sp is special. It is neither caller- nor callee-saved in the ABI, but
 *   switching tasks obviously requires switching stacks.
 *
 * Field order matches the assembly offsets in context_switch.S:
 *   x19 @ +0,  x20 @ +8,  x21 @ +16, x22 @ +24, x23 @ +32, x24 @ +40
 *   x25 @ +48, x26 @ +56, x27 @ +64, x28 @ +72
 *   fp  @ +80 (x29),  lr @ +88 (x30),  sp @ +96
 *
 * DO NOT reorder fields without updating context_switch.S.
 */
typedef struct {
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t fp;    /* x29. Frame pointer */
    uint64_t lr;    /* x30. Link register / return address */
    uint64_t sp;    /* stack pointer */
} cpu_context_t;

/* Process control block */

typedef struct pcb {
    uint32_t        pid;           /* unique task identifier (index into runqueue) */
    task_state_t    state;         /* current scheduling state */
    cpu_context_t   ctx;           /* saved register state (valid when not running) */
    uint8_t        *stack_base;    /* lowest address of this task's stack (for bounds check) */
    size_t          stack_size;    /* stack size in bytes */
    void          (*entry)(void);  /* original entry point (stored for debug/restart) */
} pcb_t;

/* Public API */

/*
 * scheduler_init: initialise scheduler state.
 * Must be called once from kernel_main before any task_create.
 */
void scheduler_init(void);

/*
 * task_create: allocate a new task.
 *
 * Allocates a pcb_t and a TASK_STACK_SIZE stack from the kernel heap.
 * Sets up cpu_context_t so the first context switch into this task calls entry().
 *
 * Returns the new PCB pointer, or NULL on failure (out of task slots or heap).
 */
pcb_t *task_create(void (*entry)(void));

/*
 * scheduler_start: switch to the first ready task.
 *
 * Call this from kernel_main after creating all initial tasks.
 * Does not return. Execution continues inside the first task.
 */
void scheduler_start(void);

/*
 * scheduler_yield: voluntarily relinquish the CPU.
 *
 * The calling task is moved to TASK_READY; the next task in the round-robin
 * queue is switched in.  Returns when the scheduler selects this task again.
 */
void scheduler_yield(void);

/*
 * scheduler_tick: called by the timer IRQ handler each interrupt.
 *
 * Triggers a round-robin switch to the next ready task.  Preemptive: the
 * current task does not need to call this itself.
 */
void scheduler_tick(void);

/* Assembly primitive (defined in arch/arm64/context_switch.S) */

/*
 * context_switch: save old task's context; restore new task's context.
 *
 *   x0 = pointer to old pcb's cpu_context_t  (save into this)
 *   x1 = pointer to new pcb's cpu_context_t  (restore from this)
 *
 * This function is the only place where callee-saved registers are
 * explicitly manipulated.  Everything else in the scheduler is plain C.
 */
void context_switch(cpu_context_t *old, cpu_context_t *new_ctx);

#endif /* KERNEL_SCHEDULER_H */
