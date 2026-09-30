# 每章独立工程 Makefile —— 显式源文件表（CROSS 可覆盖）
# ch06 异常与中断：累计到本章的源文件共 11 个；本章链接其中可独立链接的子集。
CROSS = aarch64-linux-gnu-
CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -Iinclude -g -O0 -fno-pic -fno-pie
ASFLAGS = -Iinclude -g -fno-pic
LDFLAGS = -T linker.ld -no-pie

# 显式源文件表（按 frontier 表填写）
# 本章链接：第5章基线(start.S/uart.c/main.c) + 本章新增的 gic.c
# 本章新增但【暂不链接】：src/boot/exception.S、src/kernel/irq.c
#   - irq.o 引用 handle_syscall(ch12 syscall.c)、need_resched/preempt_from_frame(ch10 proc.c)
#   - exception.o 引用 do_sync_handler/irq_handler_c(均在 irq.c)，与 irq.o 互锁
#   二者保留在快照中，待互锁簇齐全（ch18）后进入链接。
C_SRCS = src/driver/uart.c \
         src/driver/gic.c \
         src/kernel/main.c
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
