# 第二章 开发环境搭建

## 一、概述

在开始写内核之前，我们需要先搭建好开发环境。由于我们的目标平台是 ARM64，而开发主机通常是 x86 架构，所以需要使用交叉编译工具链。同时，我们用 QEMU 来模拟 ARM64 硬件，用 GDB 来调试内核。

这一章会详细讲解如何安装和配置这些工具，并验证环境是否正常工作。完成本章后，你将拥有一个可以编译、运行和调试 ARM64 裸机程序的完整开发环境。

## 二、安装交叉编译工具链

### （一）什么是交叉编译

交叉编译是指在一种架构的主机上，编译出另一种架构的可执行程序。我们在 x86_64 的电脑上，编译出可以在 ARM64 上运行的程序，这就是交叉编译。

为什么需要交叉编译？因为我们的开发主机是 x86 架构，它不能直接运行 ARM64 的编译器。我们需要一个专门的工具链，它本身运行在 x86 上，但输出的是 ARM64 的机器码。

### （二）选择工具链

有两种常见的 ARM64 交叉编译工具链：

1. **aarch64-linux-gnu**：面向 Linux 系统的工具链，包含 glibc 等标准库。虽然我们写的是裸机程序不使用标准库，但这个工具链最容易安装，也完全够用。

2. **aarch64-none-elf**：面向裸机/嵌入式的工具链，不依赖任何操作系统，使用 newlib 等轻量级 C 库。这个工具链更"干净"，但安装稍微麻烦一些。

对于本书的学习目的，`aarch64-linux-gnu` 完全够用。我们会使用 `-ffreestanding` 和 `-nostdlib` 等编译选项，告诉编译器不依赖标准库，这样它和裸机工具链的效果是一样的。

### （三）在 Ubuntu/Debian 上安装

在 Ubuntu 或 Debian 上，可以直接通过 apt 安装：

```bash
sudo apt update
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu
```

安装完成后，验证一下：

```bash
aarch64-linux-gnu-gcc --version
```

你应该看到类似这样的输出：

```
aarch64-linux-gnu-gcc (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0
Copyright (C) 2021 Free Software Foundation, Inc.
```

同时，`aarch64-linux-gnu-ld`（链接器）、`aarch64-linux-gnu-objcopy`（格式转换）、`aarch64-linux-gnu-objdump`（反汇编）等工具也会一起安装好。

### （四）在其他系统上安装

**macOS**：可以使用 Homebrew 安装：

```bash
brew install aarch64-elf-gcc
```

注意 macOS 上的工具链前缀可能是 `aarch64-elf-` 而不是 `aarch64-linux-gnu-`。

**Windows**：建议使用 WSL2（Windows Subsystem for Linux），在 WSL2 中安装 Ubuntu，然后按照上面的 Ubuntu 步骤安装。也可以直接下载 Arm 官方的工具链压缩包，解压后配置 PATH 环境变量。

**从源码编译**：如果你想使用最新版本的工具链，可以从 Arm 官网下载预编译版本，或者使用 crosstool-ng 从源码构建。但对于学习目的，发行版提供的版本已经足够。

### （五）常用编译选项

写裸机程序时，我们通常使用以下编译选项：

```makefile
CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles \
         -mgeneral-regs-only -Iinclude -g
```

这些编译选项各有其作用。`-Wall -Wextra` 开启更多警告，帮助发现潜在问题，在内核开发中警告往往意味着 bug，应该认真对待。`-ffreestanding` 告诉编译器这是一个独立环境，不依赖标准库，编译器不会假设 main 函数的存在，也不会链接标准库。`-nostdlib` 不链接标准库，`-nostartfiles` 不链接标准启动文件（如 crt0.o），因为我们会自己写启动代码。`-mgeneral-regs-only` 只使用通用寄存器，不使用浮点和 NEON 寄存器，这样在异常处理中不需要保存浮点寄存器，大大简化代码。`-Iinclude` 添加头文件搜索路径，`-g` 生成调试信息，方便 GDB 调试。

## 三、安装 QEMU

### （一）QEMU 简介

QEMU（Quick Emulator）是一个开源的硬件模拟器和虚拟化平台。它可以模拟多种 CPU 架构（x86、ARM、MIPS、RISC-V 等）和各种机器类型。

我们使用的是 `qemu-system-aarch64`，它可以模拟一个完整的 ARM64 系统，包括 CPU、内存、串口、中断控制器、定时器等外设。

### （二）安装 QEMU

在 Ubuntu/Debian 上：

```bash
sudo apt install qemu-system-arm
```

注意包名是 `qemu-system-arm`，它包含了 32 位 ARM 和 64 位 ARM（aarch64）的系统模拟器。

安装完成后验证：

```bash
qemu-system-aarch64 --version
```

你应该看到类似 `QEMU emulator version 10.2.1` 的输出。建议使用 QEMU 5.0 或更高版本，旧版本对 `virt` 平台的支持可能不完善。**特别注意：QEMU 9.0+ 的 virt 机器把 PCIe ECAM（PCI 配置访问空间）从 32 位地址 0x3f000000 移到了 64 位地址 0x4010000000**，本书 v0.3 的 PCI 驱动（第 18 章）用双基址表兼容新旧版本。如果你使用较新的 QEMU（如 Debian/Ubuntu 打包的 10.x），请确保使用本书附带的 v0.3 代码，否则 PCI 网卡探测会失败。

在 macOS 上：

```bash
brew install qemu
```

在 Windows 上，可以从 QEMU 官网下载安装包，或者在 WSL2 中通过 apt 安装。

### （三）QEMU virt 平台介绍

我们使用 QEMU 的 `virt` 机器类型。这是一个虚拟的、通用的 ARM64 平台，不对应任何真实硬件，但它模拟了一个典型的 ARM Cortex-A 系列系统。

`virt` 平台的关键硬件信息：

| 硬件 | 详情 |
|---|---|
| CPU | ARM Cortex-A57（默认），也可以指定 cortex-a72 等 |
| 内存 | 可通过 `-m` 参数指定，本书 v0.3 使用 256MB（`-m 256M`） |
| 串口 | PL011 UART，物理地址 0x09000000，中断号 33（SPI 1） |
| 中断控制器 | GICv2（默认）或 GICv3 |
| 定时器 | ARM 架构定时器（虚拟定时器中断号 27） |
| PCIe | ECAM 0x4010000000（QEMU 9.0+）/ 0x3f000000（旧版），virtio-net 挂载 |
| 设备树 | QEMU 自动生成，通过 x0 寄存器传递给内核 |

这些信息在后续章节中会反复用到。你可以通过以下命令查看 `virt` 平台支持的所有设备：

```bash
qemu-system-aarch64 -M virt -machine help
```

### （四）常用 QEMU 启动参数

运行我们的内核时，典型的 QEMU 命令如下（本书 v0.3 的 `make run`）：

```bash
qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -m 256M \
    -nographic \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -kernel kernel.elf
```

各参数含义：

- `-M virt`：使用 virt 机器类型。
- `-cpu cortex-a57`：模拟 Cortex-A57 CPU。也可以用 `cortex-a72`，默认就是 `cortex-a57`。
- `-m 256M`：分配 256MB 内存。v0.3 的页分配器管理 128MB（0x40000000~0x48000000），用户程序槽位区占 0x48000000~0x48800000，256MB 内存保证两者都有空间。
- `-nographic`：不显示图形窗口，将串口重定向到终端。这是我们最常用的模式。
- `-netdev user,id=net0 -device virtio-net-pci,netdev=net0`：添加用户模式网络和 virtio-net 网卡（PCI 形态）。第 18 章的网络实验需要它。
- `-kernel kernel.elf`：指定要加载的内核镜像。QEMU 会把它加载到内存的 0x40080000 地址（默认加载地址），然后从那里开始执行。

其他有用的参数：

- `-s`：开启 GDB 调试服务器，监听 1234 端口。
- `-S`：启动后暂停 CPU，等待 GDB 连接。通常和 `-s` 一起使用。
- `-smp 4`：模拟 4 个 CPU 核心。前期我们只用单核，后面可以尝试多核。
- `-d int`：打印中断相关的调试信息，调试中断问题时很有用。
- `-d cpu`：打印每次 CPU 状态变化，调试启动问题时有用，但输出非常多。

### （五）退出 QEMU

在 `-nographic` 模式下，QEMU 会把串口输出直接打印到终端，同时你的键盘输入会发送到串口。要退出 QEMU，按 `Ctrl+A` 然后按 `X`（先按 Ctrl+A，松开后再按 X）。

`Ctrl+A` 是 QEMU 的转义键，之后可以按不同的键执行不同操作：

- `Ctrl+A` 然后 `X`：退出 QEMU。
- `Ctrl+A` 然后 `C`：进入 QEMU 监控器（monitor），可以执行各种命令。
- `Ctrl+A` 然后 `?`：查看所有快捷键。

## 四、安装 GDB

### （一）为什么需要专门的 GDB

系统自带的 `gdb` 通常只能调试和主机相同架构的程序。要调试 ARM64 程序，需要一个支持 ARM64 的 GDB。

在 Ubuntu 上，可以安装 `gdb-multiarch`，它支持多种架构，包括 ARM64：

```bash
sudo apt install gdb-multiarch
```

验证：

```bash
gdb-multiarch --version
```

有些发行版也提供 `aarch64-linux-gnu-gdb`，效果是一样的。

### （二）QEMU + GDB 调试流程

调试内核的典型流程：

1. 用 `-s -S` 参数启动 QEMU：

```bash
qemu-system-aarch64 -M virt -m 128M -nographic -kernel kernel.elf -s -S
```

QEMU 会启动但暂停 CPU，等待 GDB 连接。

2. 在另一个终端启动 GDB：

```bash
gdb-multiarch kernel.elf
```

3. 在 GDB 中连接 QEMU：

```gdb
(gdb) target remote :1234
```

4. 设置断点，比如在 `kernel_main` 函数处：

```gdb
(gdb) break kernel_main
```

5. 继续执行：

```gdb
(gdb) continue
```

程序会运行到断点处暂停。

### （三）常用 GDB 命令

调试内核时最常用的 GDB 命令：

| 命令 | 缩写 | 作用 |
|---|---|---|
| `break <位置>` | `b` | 设置断点。可以是函数名、文件名:行号、地址 |
| `continue` | `c` | 继续执行 |
| `next` | `n` | 单步执行，跳过函数调用 |
| `step` | `s` | 单步执行，进入函数调用 |
| `finish` | `fin` | 执行到当前函数返回 |
| `info registers` | `i r` | 查看所有寄存器 |
| `print <表达式>` | `p` | 打印变量或表达式的值 |
| `x/<n><f><u> <地址>` | `x` | 查看内存内容。如 `x/16xw 0x40080000` 查看 16 个十六进制字 |
| `backtrace` | `bt` | 查看调用栈 |
| `list` | `l` | 显示当前位置附近的源码 |
| `delete <编号>` | `d` | 删除断点 |
| `quit` | `q` | 退出 GDB |

### （四）调试技巧

**查看 ARM64 特殊寄存器**：ARM64 有很多系统寄存器（如 TTBR0_EL1、SCTLR_EL1 等），可以用以下方式查看：

```gdb
(gdb) print/x $sctlr_el1
(gdb) print/x $ttbr0_el1
(gdb) print/x $currentel
```

注意寄存器名是小写的。

**反汇编**：

```gdb
(gdb) disassemble kernel_main
```

这会显示函数的汇编代码，在调试底层问题时非常有用。

**查看内存**：

```gdb
(gdb) x/32xw $sp
```

查看栈指针指向的 32 个字（128 字节）内存。

## 五、其他工具

### （一）Make 和构建系统

我们使用 Make 来管理构建过程。Ubuntu 上通常已经预装了 Make，如果没有：

```bash
sudo apt install make
```

### （二）设备树编译器

QEMU 的 `virt` 平台会自动生成设备树（Device Tree）并传递给内核。设备树描述了硬件的布局信息。虽然我们前期会硬编码硬件地址，但了解设备树是有好处的。

安装设备树编译器：

```bash
sudo apt install device-tree-compiler
```

可以用以下命令查看 QEMU 生成的设备树：

```bash
qemu-system-aarch64 -M virt -m 128M -nographic -dumpdtb virt.dtb
dtc -I dtb -O dts virt.dtb
```

这会输出设备树的文本表示，你可以看到串口、中断控制器、内存等硬件的信息。

### （三）文件查看工具

`objdump` 可以用来查看目标文件和可执行文件的内容：

```bash
# 反汇编
aarch64-linux-gnu-objdump -d kernel.elf

# 查看段信息
aarch64-linux-gnu-objdump -h kernel.elf

# 查看符号表
aarch64-linux-gnu-objdump -t kernel.elf
```

`readelf` 可以查看 ELF 文件的详细信息：

```bash
aarch64-linux-gnu-readelf -a kernel.elf
```

`nm` 可以查看符号表：

```bash
aarch64-linux-gnu-nm kernel.elf
```

## 六、验证环境

现在让我们写一个最简单的程序来验证整个工具链是否正常工作。

### （一）编写测试程序

创建一个工作目录，比如 `os-dev`，然后在里面创建一个 `hello.c`：

```c
// hello.c
volatile unsigned int * const UART0DR = (unsigned int *)0x09000000;

void print_string(const char *s) {
    while (*s != '\0') {
        *UART0DR = (unsigned int)(*s);
        s++;
    }
}

void kernel_main(void) {
    print_string("Hello, ARM64!\n");
    while (1) {
        // 死循环
    }
}
```

这个程序直接往 PL011 UART 的数据寄存器地址（0x09000000）写字符，实现串口输出。我们还没有写启动代码和链接脚本，所以先用 QEMU 的内置加载器来测试。

### （二）编译

```bash
aarch64-linux-gnu-gcc -ffreestanding -nostdlib -nostartfiles \
    -mgeneral-regs-only -c hello.c -o hello.o
```

但只有 C 文件还不够，我们需要一个入口点。QEMU 加载内核后会从加载地址开始执行，而 C 函数的入口不一定在最前面。让我们写一个简单的汇编启动文件 `start.S`：

```assembly
// start.S
.section .text.start
.global _start
_start:
    // 设置栈指针
    ldr x0, =0x40100000
    mov sp, x0
    // 调用 C 函数
    bl kernel_main
    // 如果返回，死循环
1:
    b 1b
```

然后写一个简单的链接脚本 `link.ld`：

```
ENTRY(_start)

SECTIONS {
    . = 0x40080000;

    .text : {
        *(.text.start)
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        __bss_end = .;
    }
}
```

现在编译链接：

```bash
aarch64-linux-gnu-gcc -ffreestanding -nostdlib -nostartfiles \
    -mgeneral-regs-only -c hello.c -o hello.o
aarch64-linux-gnu-gcc -ffreestanding -nostdlib -nostartfiles \
    -mgeneral-regs-only -c start.S -o start.o
aarch64-linux-gnu-ld -T link.ld start.o hello.o -o kernel.elf
```

### （三）运行

```bash
qemu-system-aarch64 -M virt -m 128M -nographic -kernel kernel.elf
```

如果一切正常，你应该看到输出：

```
Hello, ARM64!
```

按 `Ctrl+A` 然后 `X` 退出 QEMU。

### （四）用 GDB 调试

让我们用 GDB 来调试这个程序，验证调试环境是否正常。

终端 1 启动 QEMU：

```bash
qemu-system-aarch64 -M virt -m 128M -nographic -kernel kernel.elf -s -S
```

终端 2 启动 GDB：

```bash
gdb-multiarch kernel.elf
```

在 GDB 中：

```gdb
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```

程序应该会在 `kernel_main` 函数处暂停。你可以用 `list` 查看源码，用 `next` 单步执行，用 `print` 查看变量。确认一切正常后，输入 `quit` 退出 GDB。

## 七、项目目录结构

为了方便后续的开发，建议按照以下结构组织代码：

```
os-dev/
├── Makefile
├── link.ld
├── include/
│   ├── kernel/
│   ├── driver/
│   └── arch/
├── src/
│   ├── boot/
│   │   └── start.S
│   ├── kernel/
│   ├── driver/
│   ├── mm/
│   ├── proc/
│   └── fs/
└── build/
    └── (编译产物)
```

- `Makefile`：构建脚本。
- `link.ld`：链接脚本。
- `include/`：头文件，按模块分类。
- `src/`：源代码，按模块分类。
  - `boot/`：启动汇编代码。
  - `kernel/`：内核核心代码。
  - `driver/`：设备驱动。
  - `mm/`：内存管理。
  - `proc/`：进程管理。
  - `fs/`：文件系统。
- `build/`：编译产物，不纳入版本控制。

随着开发的进行，这个目录结构会不断丰富。每一章的代码都可以在这个基础上扩展。

## 八、常见问题

### （一）QEMU 启动后没有输出

可能的原因有几个。程序可能没有正确加载，需要检查链接脚本中的起始地址是否是 0x40080000，这是 QEMU `-kernel` 的默认加载地址。串口地址可能错误，virt 平台的 PL011 UART 地址是 0x09000000，不是其他平台的地址。栈可能没有正确设置，在调用 C 函数前必须设置栈指针，否则函数调用会崩溃。程序也可能跑飞了，用 GDB 单步调试可以看看程序执行到了哪里。

### （二）GDB 连接失败

GDB 连接失败时，首先确认 QEMU 加了 `-s` 参数，这个参数让 QEMU 监听 1234 端口等待 GDB 连接。然后确认端口 1234 没有被其他程序占用，可以用 `lsof -i :1234` 检查。最后确认 GDB 加载了正确的 ELF 文件，而且这个 ELF 文件带有调试信息（编译时加了 `-g` 参数）。

### （三）编译报错找不到头文件

检查 `-I` 参数是否正确指向了 `include` 目录。裸机环境下不能使用标准库头文件（如 `stdio.h`、`stdlib.h`），需要自己实现需要的功能。

### （四）QEMU 版本太旧

某些功能（如 GICv3）需要较新版本的 QEMU。如果遇到奇怪的问题，尝试升级 QEMU 到最新版本。

## 十、GDB 调试实战技巧

掌握 GDB 调试是内核开发的必备技能。以下是一些实用的调试技巧。

查看 ARM64 特殊寄存器。除了通用寄存器，ARM64 有大量系统寄存器。在 GDB 中可以用 `print/x $sctlr_el1` 查看系统控制寄存器，`print/x $ttbr0_el1` 查看页表基地址，`print/x $currentel` 查看当前异常级别，`print/x $spsr_el1` 查看保存的程序状态。注意寄存器名是小写的。

查看内存内容。`x/16xw 0x40080000` 以十六进制字（4字节）为单位查看 0x40080000 开始的 16 个字。`x/32xb $sp` 查看栈指针指向的 32 个字节。`x/8i $pc` 反汇编当前 PC 附近的 8 条指令。`x/s 0x40090000` 查看以 null 结尾的字符串。

设置条件断点。`break kernel_main if count == 5` 只在 count 等于 5 时中断。`watch variable` 在变量被修改时中断（数据断点），这在调试内存破坏问题时非常有用。`rwatch` 在变量被读取时中断，`awatch` 在读写时都中断。

自定义 GDB 命令。可以在 ~/.gdbinit 中定义常用的命令序列。比如定义一个命令打印当前进程信息：

```
define dump_task
    print *current
    print/x $sp
    print/x $pc
end
```

使用 GDB 的 TUI（Text User Interface）模式。启动时加 `-tui` 参数，或者在 GDB 中按 `Ctrl+X A`，会显示源码窗口和汇编窗口。`layout split` 同时显示源码和汇编，`layout regs` 显示寄存器窗口。这在单步调试时非常直观。

远程调试的注意事项。QEMU 的 GDB 服务器默认监听 1234 端口。如果端口被占用，可以用 `-gdb tcp::1235` 指定其他端口。连接后用 `set architecture aarch64` 确保 GDB 使用正确的架构。加载符号表用 `file kernel.elf`，这样源码级调试才能正常工作。

## 十一、QEMU 高级用法

除了基本的启动参数，QEMU 还有很多高级功能可以帮助开发和调试。

监控器（Monitor）。在 QEMU 运行时按 `Ctrl+A C` 进入监控器，可以执行各种命令。`info registers` 查看所有寄存器，`info mem` 查看内存映射，`xp /16xw 0x40080000` 查看物理内存（注意是 xp 不是 x，x 查看虚拟内存），`dump-guest-memory` 转储客户机内存，`quit` 退出 QEMU。

调试输出。`-d int` 打印中断相关信息，包括中断触发、异常进入和返回。`-d cpu` 打印每次 CPU 状态变化（非常详细，输出量大）。`-d mmu` 打印 MMU 相关操作。`-d unimp` 打印未实现的功能访问。可以用 `-d help` 查看所有可用的调试选项。调试输出默认打印到 stderr，可以用 `-D logfile` 重定向到文件。

快照（Snapshot）。`-snapshot` 参数让 QEMU 以快照模式运行，所有对磁盘的修改都不会写入磁盘文件，退出后自动丢弃。这在测试文件系统时很有用，可以反复实验而不破坏磁盘镜像。在监控器中用 `savevm name` 保存快照，`loadvm name` 恢复快照。

多机网络。`-netdev user,id=net0 -device virtio-net-device,netdev=net0` 给虚拟机添加用户模式网络。用户模式网络提供 NAT 访问，虚拟机可以访问外网，但外部不能直接访问虚拟机。`-netdev socket` 可以连接多个 QEMU 实例，组建虚拟网络。

指定 CPU 和机器特性。`-cpu max` 模拟支持最多特性的 CPU。`-machine virt,gic-version=3` 使用 GICv3 中断控制器。`-machine virt,virtualization=on` 开启虚拟化支持（EL2）。`-smp 4` 模拟 4 个 CPU 核心。这些选项在测试特定功能时很有用。

## 十二、本章小结

这一章我们搭建了完整的开发环境。我们安装了 aarch64-linux-gnu 交叉编译工具链，用于编译 ARM64 程序。安装了 qemu-system-aarch64，用于模拟 ARM64 硬件并运行我们的内核。安装了 gdb-multiarch，用于调试内核。我们了解了 QEMU virt 平台的硬件布局，包括 UART、GIC、内存等关键外设的地址。编写并运行了第一个裸机程序，验证了整个工具链能够正常工作。还学习了 QEMU 配合 GDB 的基本调试方法，为后续的内核开发做好了准备。

环境搭建好之后，下一章我们将深入学习 ARM64 架构的基础知识，为后续的内核开发打下坚实的基础。
