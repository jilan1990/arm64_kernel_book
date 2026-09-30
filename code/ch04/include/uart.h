// include/uart.h
#ifndef UART_H
#define UART_H

// QEMU virt 平台 PL011 UART 基地址
#define UART0_BASE  0x09000000

// 寄存器偏移
#define UART_DR     0x00
#define UART_FR     0x18

// 标志寄存器位
#define UART_FR_TXFF    (1 << 5)
#define UART_FR_RXFE    (1 << 4)

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_puthex(unsigned int v);
void uart_puthex64(unsigned long v);
void uart_puthex_byte(unsigned char v);
char uart_getc(void);

#endif
