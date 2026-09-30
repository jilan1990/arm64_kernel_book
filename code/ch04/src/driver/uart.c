// src/driver/uart.c
#include "uart.h"

// 读取寄存器
#define UART_READ(reg) \
    (*(volatile unsigned int *)(UART0_BASE + (reg)))

// 写入寄存器
#define UART_WRITE(reg, val) \
    (*(volatile unsigned int *)(UART0_BASE + (reg)) = (val))

void uart_init(void) {
    // QEMU 的 UART 已经配置好，不需要额外初始化
    // 真实硬件上需要设置波特率、数据位、停止位等
}

void uart_putc(char c) {
    // 等待发送 FIFO 不满
    while (UART_READ(UART_FR) & UART_FR_TXFF) {
        // 忙等待
    }
    // 写入数据寄存器，发送字符
    UART_WRITE(UART_DR, (unsigned int)c);
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_puthex(unsigned int v) {
    const char *hex = "0123456789abcdef";
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(v >> i) & 0xF]);
    }
}

void uart_puthex64(unsigned long v) {
    const char *hex = "0123456789abcdef";
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(v >> i) & 0xF]);
    }
}

void uart_puthex_byte(unsigned char v) {
    const char *hex = "0123456789abcdef";
    uart_putc(hex[(v >> 4) & 0xF]);
    uart_putc(hex[v & 0xF]);
}

char uart_getc(void) {
    // 等待接收 FIFO 非空
    while (UART_READ(UART_FR) & UART_FR_RXFE) {
        // 忙等待
    }
    // 读取数据寄存器
    return (char)UART_READ(UART_DR);
}
