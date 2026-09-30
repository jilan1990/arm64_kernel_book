// src/driver/uart_char.c
#include "char_dev.h"
#include "uart.h"

#define UART_MAJOR 4

static int uart_open(int minor) {
    (void)minor;
    return 0;
}

static int uart_close(int minor) {
    (void)minor;
    return 0;
}

static int uart_read(int minor, char *buf, size_t count) {
    (void)minor;
    size_t i = 0;
    while (i < count) {
        char c = uart_getc();
        buf[i++] = c;
        if (c == '\n') break;
    }
    return i;
}

static int uart_write(int minor, const char *buf, size_t count) {
    (void)minor;
    for (size_t i = 0; i < count; i++) {
        uart_putc(buf[i]);
    }
    return count;
}

static struct char_dev uart_dev = {
    .name = "ttyS0",
    .major = UART_MAJOR,
    .open = uart_open,
    .close = uart_close,
    .read = uart_read,
    .write = uart_write,
    .ioctl = NULL,
};

void uart_char_init(void) {
    char_dev_register(&uart_dev);
}
