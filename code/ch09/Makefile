# 每章独立工程 Makefile —— 显式源文件表（CROSS 可覆盖）
# ch09 内核内存分配器：累计源文件 17 个；本章链接新增 slab.c（依赖 page_alloc.o）。
CROSS = aarch64-linux-gnu-
CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -Iinclude -g -O0 -fno-pic -fno-pie
ASFLAGS = -Iinclude -g -fno-pic
LDFLAGS = -T linker.ld -no-pie

# 显式源文件表（按 frontier 表填写）
# 本章链接：ch08 链接子集 + 本章新增 slab.c
#   slab.o 仅引用 alloc_page(page_alloc.o) 与 mm.h 中的 ALIGN_*/PAGE_SIZE 宏，
#   在已有 page_alloc.o 的前提下可独立链接。
# 仍【暂不链接】：src/boot/exception.S、src/kernel/irq.c（互锁簇，ch18 才链接）。
C_SRCS = src/driver/uart.c \
         src/driver/gic.c \
         src/kernel/main.c \
         src/mm/page_alloc.c \
         src/mm/slab.c
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
