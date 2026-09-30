# 每章独立工程 Makefile —— 显式源文件表（CROSS 可覆盖）
# ch10 进程管理与上下文切换：累计源文件 19 个；本章链接新增 context_switch.S，
# proc.c 保留在快照但【暂不链接】。
CROSS = aarch64-linux-gnu-
CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -Iinclude -g -O0 -fno-pic -fno-pie
ASFLAGS = -Iinclude -g -fno-pic
LDFLAGS = -T linker.ld -no-pie

# 显式源文件表（按 frontier 表填写）
# 本章链接：ch09 链接子集 + 本章新增 context_switch.S
#   context_switch.o 只定义 context_switch/switch_to，不引用任何外部符号，可独立链接。
#
# 本章新增但【暂不链接】：src/proc/proc.c
#   proc.o 引用 interrupt_return(exception.S 簇)、get_ticks(ch11 timer)、
#   ramdisk_init/uart_char_init(ch14)、shell/hello/test/net_prog_*(ch16 embed.S)；
#   与 irq.c/exception.S/timer.c/syscall.c 构成互锁簇，直到 ch18 全栈齐备才可链接。
# 仍【暂不链接】：src/boot/exception.S、src/kernel/irq.c（同上互锁簇）。
C_SRCS = src/driver/uart.c \
         src/driver/gic.c \
         src/kernel/main.c \
         src/mm/page_alloc.c \
         src/mm/slab.c
ASM_SRCS = src/boot/start.S \
           src/proc/context_switch.S
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
