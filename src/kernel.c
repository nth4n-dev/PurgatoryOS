/* src/kernel.c */
#include "drivers/uart.h"

void kernel_main(void) {
    uart_init();
    kprint("Hello, Kernel!\n");

    while (1);
}
