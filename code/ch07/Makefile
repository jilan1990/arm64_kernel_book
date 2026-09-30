# 每章独立工程 Makefile —— 显式源文件表（CROSS 可覆盖）
# ch07 物理内存管理：累计源文件 13 个；本章链接新增 page_alloc.c。
CROSS = aarch64-linux-gnu-
CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -Iinclude -g -O0 -fno-pic -fno-pie
ASFLAGS = -Iinclude -g -fno-pic
LDFLAGS = -T linker.ld -no-pie

# 显式源文件表（按 frontier 表填写）
# 本章链接：ch06 链接子集(start.S/uart.c/gic.c/main.c) + 本章新增 page_alloc.c
#   page_alloc.o 仅引用 _end(linker.ld) 与 uart_*(uart.o)，可独立链接。
# 仍【暂不链接】：src/boot/exception.S、src/kernel/irq.c（原因见 ch06 README；
#   irq.o 还需 handle_syscall→ch12、need_resched/preempt_from_frame→ch10 proc.c；
#   exception.o 依赖 irq.o，二者互锁，ch18 才随全栈链接）。
C_SRCS = src/driver/uart.c \
         src/driver/gic.c \
         src/kernel/main.c \
         src/mm/page_alloc.c
ASM_SRCS = src/boot/start.S
OBJS = $(C_SRCS:.c=.o) $(ASM_SRCS:.S=.o)
TARGET = kernel.elf

.PHONY: all clean run
all: $(TARGET)

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	$(OBJDUMP) -d $@ > kernel.dis

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
%.o: %.S
	$(CC) $(ASFLAGS) -c $< -o $@

run: $(TARGET)
	qemu-system-aarch64 -M virt -m 256M -nographic -cpu cortex-a57 -kernel $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET) kernel.dis
