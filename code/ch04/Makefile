# ch04 Makefile — 只编译 uart.o 与 main.o，不链接 kernel.elf
# 本章验证 UART 驱动与最小入口函数可编译；可启动镜像需第 5 章 start.S/linker.ld

CROSS = aarch64-none-elf-
CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -Iinclude -g -O0 -fno-pic -fno-pie

# 本章只编译这两个目标文件（不链接）
C_SRCS = src/driver/uart.c src/kernel/main.c
OBJS = $(C_SRCS:.c=.o)

.PHONY: all clean
all: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
