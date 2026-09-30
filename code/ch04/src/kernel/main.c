// src/kernel/main.c
#include "uart.h"

void kernel_main(void) {
    uart_init();
    uart_puts("Hello, MyOS!\n");
    uart_puts("Chapter 4: UART Hello World\n");
    while (1) { }
}
