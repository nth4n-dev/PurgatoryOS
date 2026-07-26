/* src/kernel/timer.c */
#include "kernel/timer.h"
#include "kernel/gic.h"
#include "kernel/scheduler.h"
#include "drivers/uart.h"

static volatile uint64_t tick_count = 0;
static uint64_t reload_ticks = 0;   /* saved so tick() can re-arm */

void timer_init(uint32_t interval_ms) {
    /* 1. Read the counter frequency */
    uint64_t freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));

    /* 2. Calculate the number of ticks for the desired interval */
    reload_ticks = (freq * (uint64_t)interval_ms) / 1000ULL;

    /* 3. Write the countdown value. Timer fires when the counter
     *    has advanced by this many ticks from now. */
    asm volatile("msr cntp_tval_el0, %0" :: "r"(reload_ticks));

    /* 4. Enable the timer and unmask its interrupt (bit 0 = enable,
     *    bit 1 = IMASK=0 means the interrupt is NOT masked) */
    asm volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)1));

    /* 5. Enable INTID 30 in the GIC so the timer can signal the CPU */
    gic_enable_irq(TIMER_IRQ);
}

void timer_tick(void) {
    tick_count++;

    /* Print a simple tick message */
    kprint("tick #");
    /* Quick inline decimal print for small numbers */
    uint64_t n = tick_count;
    char buf[20];
    int i = 0;
    if (n == 0) { buf[i++] = '0'; }
    else {
        while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
        /* reverse */
        for (int l = 0, r = i - 1; l < r; l++, r--) {
            char t = buf[l]; buf[l] = buf[r]; buf[r] = t;
        }
    }
    buf[i] = '\0';
    kprint(buf);
    kprint("\n");

    /* Re-arm: write the same countdown so the timer fires again */
    asm volatile("msr cntp_tval_el0, %0" :: "r"(reload_ticks));

    /* Preemptive scheduling: let the scheduler pick the next task.
     * The timer IRQ is the "heartbeat" that drives preemption.
     * scheduler_tick() calls context_switch() if another task is ready. */
    scheduler_tick();
}

uint64_t timer_get_ticks(void) {
    return tick_count;
}
