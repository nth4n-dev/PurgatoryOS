/* src/drivers/uart.c */
#include "drivers/uart.h"

void uart_init(void) {
    /* Disable the UART before reconfiguring */
    UART_REG(UARTCR) = 0;

    /*
     * Set baud rate. QEMU's virt machine uses a 24 MHz UART clock.
     * For 115200 baud:   divisor = 24000000 / (16 * 115200) = 13.020833...
     *   Integer part:    13, so UARTIBRD = 13
     *   Fractional part: 0.020833 * 64 is about 1, so UARTFBRD = 1
     *
     * QEMU ignores the baud rate, so the UART works without this.
     * Real hardware does not. We set it properly anyway.
     */
    UART_REG(UARTIBRD)  = 13;
    UART_REG(UARTFBRD)  = 1;

    /*
     * Line control: 8-bit word length, FIFOs enabled, 1 stop bit, no parity.
     * The UARTLCR_H write must come after the baud rate. Writing
     * UARTLCR_H latches the baud rate divisors into the internal registers.
     */
    UART_REG(UARTLCR_H) = UARTLCR_WLEN8 | UARTLCR_FEN;

    /* Re-enable: UART on, TX on, RX on */
    UART_REG(UARTCR) = UARTCR_UARTEN | UARTCR_TXE | UARTCR_RXE;
}

void uart_putc(char c) {
    /* Spin until the transmit FIFO has space */
    while (UART_REG(UARTFR) & UARTFR_TXFF)
        ;
    UART_REG(UARTDR) = (uint32_t)c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

void kprint(const char *s) {
    uart_puts(s);
}
