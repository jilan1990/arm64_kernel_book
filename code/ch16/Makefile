# 每章独立工程 Makefile —— 显式源文件表（CROSS 可覆盖）
# ch16 用户空间与 Shell：此前可链接子集 + embed.o，main.c 为书稿第四章最小版
CROSS = aarch64-linux-gnu-
CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

# 内核编译选项
CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -Iinclude -g -O0 -fno-pic -fno-pie
ASFLAGS = -Iinclude -g -fno-pic
LDFLAGS = -T linker.ld -no-pie

# 用户程序编译选项（PC 相对寻址，可加载到任意对齐槽位）
USER_CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -O0 -g -fno-pic -fno-pie
USER_LDFLAGS = -T user/user_link.ld -no-pie

# 显式源文件表（本章链接/构建的源文件；其余 .c/.S 在快照中但暂不链接，见 README）
C_SRCS = src/driver/uart.c          \
         src/driver/gic.c           \
         src/mm/page_alloc.c        \
         src/mm/slab.c              \
         src/kernel/spinlock.c      \
         src/driver/char_dev.c      \
         src/driver/uart_char.c     \
         src/driver/ramdisk.c       \
         src/fs/tmpfs.c             \
         src/kernel/main.c

ASM_SRCS = src/boot/start.S         \
           src/proc/context_switch.S\
           src/boot/embed.S

OBJS = $(C_SRCS:.c=.o) $(ASM_SRCS:.S=.o)
TARGET = kernel.elf

# 用户程序
USER_PROGS = shell hello test net
USER_BINS = $(addprefix user/,$(addsuffix .bin,$(USER_PROGS)))

.PHONY: all clean run
all: $(TARGET) kernel.bin

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	$(OBJDUMP) -d $@ > kernel.dis

kernel.bin: $(TARGET)
	$(OBJCOPY) -O binary $(TARGET) kernel.bin

# 用户程序先于 embed.o 构建（embed.S 的 .incbin 需要 bin 存在）
src/boot/embed.o: $(USER_BINS)

user/%.bin: user/%.elf
	$(OBJCOPY) -O binary $< $@

user/shell.elf: user/shell.c user/user_syscall.h user/user_link.ld
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/shell.c

user/hello.elf: user/hello.c user/user_syscall.h user/user_link.ld
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/hello.c

user/test.elf: user/test.S user/user_link.ld
	$(CC) -ffreestanding -nostdlib -nostartfiles -g $(USER_LDFLAGS) -o $@ user/test.S

user/net.elf: user/net.c user/user_syscall.h user/user_link.ld
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/net.c

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(ASFLAGS) -c $< -o $@

run: $(TARGET)
	qemu-system-aarch64 -M virt -m 256M -nographic -cpu cortex-a57 -kernel $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET) kernel.bin kernel.dis user/*.elf user/*.bin
