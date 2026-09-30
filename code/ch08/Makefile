# 每章独立工程 Makefile —— 显式源文件表（CROSS 可覆盖）
# ch08 内存布局与 MMU 设计：累计源文件 15 个；本章只新增头文件(proc.h/mmu.h)，无新 .o。
CROSS = aarch64-linux-gnu-
CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -Iinclude -g -O0 -fno-pic -fno-pie
ASFLAGS = -Iinclude -g -fno-pic
LDFLAGS = -T linker.ld -no-pie

# 显式源文件表（按 frontier 表填写）
# 本章链接：与 ch07 完全一致（无新增可构建目标）。
#   include/proc.h、include/mmu.h 为头文件，不产生 .o；mmu_init 占位实现早已
#   定义在 page_alloc.c 底部（恒等映射、未启用 MMU）。
# 仍【暂不链接】：src/boot/exception.S、src/kernel/irq.c（互锁簇，ch18 才链接）。
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
