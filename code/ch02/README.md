# ch02 开发环境搭建 — 本章代码快照

## 本章主题
安装交叉编译工具链、QEMU、GDB，验证整个开发链路可用。

## 本章无独立内核代码
本章不包含内核源码，只给出环境搭建命令和一个最小验证程序（非构建文件，仅作教学演示）。下一章（ch03）讲解 ARM64 架构基础，ch04 才开始写真正的裸机代码。

---

### 环境搭建命令（非构建文件，摘自书稿）

**Ubuntu/Debian 安装工具链：**
```bash
sudo apt update
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu
```

**安装 QEMU：**
```bash
sudo apt install qemu-system-arm
```

**安装 GDB：**
```bash
sudo apt install gdb-multiarch
```

### 本书实际使用的工具链
本书快照工程统一使用 Arm 官方预编译的 `aarch64-none-elf-` 裸机工具链，路径：
```
/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
```

### QEMU virt 平台关键参数（摘自书稿）

| 硬件 | 详情 |
|---|---|
| CPU | ARM Cortex-A57 |
| 内存 | 256MB（`-m 256M`） |
| 串口 | PL011 UART，物理地址 `0x09000000`，中断号 33 |
| 中断控制器 | GICv2 |
| 定时器 | ARM 架构定时器（虚拟定时器中断号 27） |
| PCIe ECAM | `0x4010000000`（QEMU 9.0+） |

典型启动命令：
```bash
qemu-system-aarch64 -M virt -cpu cortex-a57 -m 256M -nographic -kernel kernel.elf
```

### 裸机编译选项（非构建文件，摘自书稿）
```makefile
CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles \
         -mgeneral-regs-only -Iinclude -g -O0 -fno-pic -fno-pie
```

### 下一章起点
ch03 讲解 ARM64 架构基础（寄存器、异常级别、汇编指令、内存模型、调用约定），为后续写启动代码和驱动做准备。
