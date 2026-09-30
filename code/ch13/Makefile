# 每章独立工程 Makefile —— 显式源文件表（CROSS 可覆盖）
CROSS = aarch64-linux-gnu-
CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -Iinclude -g -O0 -fno-pic -fno-pie
ASFLAGS = -Iinclude -g -fno-pic
LDFLAGS = -T linker.ld -no-pie

# 显式源文件表（按 frontier 表填）
# ch13：在 ch12 链接集上新增 spinlock.c
# semaphore.c 已引入但暂不链接：依赖 current/schedule（属 proc.o，ch10 引入，互锁簇 ch18 才全链接）
C_SRCS = src/driver/uart.c \
         src/driver/gic.c \
         src/mm/page_alloc.c \
         src/mm/slab.c \
         src/kernel/main.c \
         src/kernel/spinlock.c
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
