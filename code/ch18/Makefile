# 工具链前缀
CROSS = aarch64-linux-gnu-

# 工具
CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

# 内核编译选项
CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles          -mgeneral-regs-only -Iinclude -g -O0 -fno-pic -fno-pie
ASFLAGS = -Iinclude -g -fno-pic
LDFLAGS = -T linker.ld -no-pie

# 用户程序编译选项（PC 相对寻址，可加载到任意对齐槽位）
USER_CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles               -mgeneral-regs-only -O0 -g -fno-pic -fno-pie
USER_LDFLAGS = -T user/user_link.ld -no-pie

# 内核源文件
C_SRCS = $(wildcard src/kernel/*.c)          $(wildcard src/kernel/net/*.c)          $(wildcard src/driver/*.c)          $(wildcard src/mm/*.c)          $(wildcard src/proc/*.c)          $(wildcard src/fs/*.c)
ASM_SRCS = $(wildcard src/boot/*.S)            $(wildcard src/proc/*.S)

C_OBJS = $(C_SRCS:.c=.o)
ASM_OBJS = $(ASM_SRCS:.S=.o)
OBJS = $(ASM_OBJS) $(C_OBJS)

TARGET = kernel.elf

# 用户程序
USER_PROGS = shell hello test net
USER_BINS = $(addprefix user/,$(addsuffix .bin,$(USER_PROGS)))

.PHONY: all clean run debug run-bin

all: $(TARGET) kernel.bin

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	$(OBJDUMP) -d $@ > kernel.dis

kernel.bin: $(TARGET)
	$(OBJCOPY) -O binary $(TARGET) kernel.bin

# 用户程序先于 embed.o 构建
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
	qemu-system-aarch64 -M virt -m 256M -nographic -cpu cortex-a57 		-netdev user,id=net0 		-device virtio-net-pci,netdev=net0 		-kernel $(TARGET)

run-bin: kernel.bin
	qemu-system-aarch64 -M virt -m 256M -nographic -cpu cortex-a57 		-netdev user,id=net0 -device virtio-net-pci,netdev=net0 		-device loader,addr=0x40080000,kernel=kernel.bin

debug: $(TARGET)
	qemu-system-aarch64 -M virt -m 256M -nographic -cpu cortex-a57 		-netdev user,id=net0 -device virtio-net-pci,netdev=net0 		-kernel $(TARGET) -s -S

clean:
	rm -f $(OBJS) $(TARGET) kernel.bin kernel.dis 		user/*.elf user/*.bin
