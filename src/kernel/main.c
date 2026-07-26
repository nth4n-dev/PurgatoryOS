/* src/kernel/main.c */
#include "drivers/uart.h"
#include "kernel/mmu.h"

void kernel_main(void) {
    mmu_init();
    uart_init();

    kprint("Hello, Kernel!\n");

    /* Spin. There is nothing else to do yet. */
    while (1);
}
