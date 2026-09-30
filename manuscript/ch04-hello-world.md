# 第四章 第一个裸机程序：Hello World

## 一、概述

在没有操作系统的情况下，CPU 上电后执行的第一段代码就是裸机程序。这一章我们从最基础的地方开始：让 CPU 通过串口输出 "Hello World"。虽然简单，但这一步验证了整个开发链路——交叉编译、链接、镜像加载、QEMU 运行——是否通畅，也让我们第一次接触 ARM64 的内存映射硬件（MMIO）。

本章代码对应最终版本中 `src/driver/uart.c`、`include/uart.h` 以及最小化的 `main.c` 和 `Makefile`。在本书的代码目录中，`code/ch04/` 是一个完整可编译运行的最小项目。

在开始之前，先明确"裸机程序"（bare-metal program）的含义：它不依赖任何操作系统、不链接标准库、没有进程环境，从第一条指令起就完全掌控硬件。我们写内核的过程，本质上就是在裸机程序的基础上不断叠加操作系统组件。裸机开发与普通应用开发最大的区别在于两点：其一，**没有调试器帮你拦截未定义行为**，任何错误（比如访问非法地址、死循环）都会直接造成 CPU 跑飞或死机；其二，**没有抽象层**，所有硬件访问都是直接的寄存器读写。这两点决定了裸机代码必须"小而正确"——这正是本书坚持逐章小步前进、每章都可编译运行的原因。

## 二、QEMU virt 平台的硬件布局

QEMU 的 `virt` 机器为 ARM64 提供了一个标准化的虚拟硬件平台。与树莓派等真实硬件不同，virt 平台的设备地址是固定的、文档化的，非常适合教学。本书用到的核心硬件地址：

| 设备 | 基地址 | 说明 |
|---|---|---|
| PL011 UART | 0x09000000 | 串口，输出调试信息 |
| GIC 分发器 | 0x08000000 | 中断控制器（第 6 章） |
| GIC CPU 接口 | 0x08010000 | 中断控制器 CPU 侧（第 6 章） |
| RAM | 0x40000000 | 物理内存（256MB，页分配器管理其中 128MB） |
| 内核加载地址 | 0x40080000 | QEMU `-kernel` 把内核镜像放在这里 |
| PCIe ECAM | 0x4010000000 | PCI 配置空间（第 18 章） |

这些地址来自 QEMU 的 virt 机器定义（`hw/arm/virt.c`）。在我们的代码中，这些常量定义在 `include/uart.h`、`include/gic.h`、`include/mm.h` 等头文件里。

**内存映射 I/O（MMIO）**：ARM64 不像 x86 那样有独立的 I/O 端口地址空间，所有外设寄存器都映射到物理地址空间。访问串口就是直接对 0x09000000 附近的物理地址进行读写。这要求我们对物理地址做 `volatile` 指针访问，防止编译器优化掉或乱序访问。

理解 MMIO 的关键是区分"内存"与"寄存器"两种访问对象。普通内存的读写是无副作用的：读一个地址 N 次得到相同值，写一个地址只改变该地址的内容。寄存器则不同——读 `UART_DR` 可能消耗接收 FIFO 的一个字符（副作用），写 `UART_DR` 会把字符送入发送 FIFO。正因为存在副作用，编译器绝不能把两次相邻的寄存器访问合并成一次，也绝不能因为"看起来没人用"而删除一次寄存器写。C 语言的 `volatile` 关键字就是为这种情况设计的：它告诉编译器，这个地址的内容可能在"程序视角之外"变化（硬件、DMA、中断），每次访问都必须真实发生、按源码顺序发生。

对比 x86：x86 有独立的 IN/OUT 指令和 I/O 端口空间（0x0000~0xFFFF），设备通常同时提供端口映射和内存映射两种访问方式。ARM 从一开始就选择纯内存映射，外设地址直接占据物理地址空间的一部分。这意味着在 ARM 上，地址总线上的每一个地址要么是 RAM、要么是某个外设寄存器，驱动代码与普通指针操作在形式上没有区别——唯一的区别就是加不加 `volatile`。

v0.3 内核中还有一处相关细节：第 6 章的 GIC、第 11 章的定时器、第 18 章的 virtio 全部沿用"基地址 + 偏移"的 MMIO 访问模式。把 MMIO 的心智模型在这一章建立起来，后续所有驱动都能用同一套思路去读。

## 三、PL011 UART 驱动

ARM 的 PL011 是一个广泛使用的串口控制器。QEMU 在 virt 平台上模拟的正是它。我们只需要关心两个寄存器：

- `UART_DR`（偏移 0x00）：数据寄存器。写一个字节就是发送，读一个字节就是接收。
- `UART_FR`（偏移 0x18）：标志寄存器。`TXFF`（bit 5）表示发送 FIFO 已满，`RXFE`（bit 4）表示接收 FIFO 为空。

完整的 PL011 寄存器空间不止这两个，但裸机起步只需要它们。真实硬件上还要配置 `UART_IBRD`/`UART_FBRD`（波特率分频）、`UART_LCRH`（帧格式）、`UART_CR`（使能控制）等寄存器；QEMU 的 virt 平台默认已经把 PL011 配置为 115200 8N1 并处于使能状态，所以我们不需要写初始化代码——把这一点说清楚，读者将来面对真实硬件时才知道要补什么。

发送一个字符的流程：先检查发送 FIFO 是否满（忙等待），不满则把字符写入 `UART_DR`。用伪代码描述：

```text
循环：读取 UART_FR，若 TXFF 位为 1（FIFO 满），继续循环（忙等待）
FIFO 有空位后：把字符写入 UART_DR
```

这是一个典型的"忙等待轮询"（polling）模型：CPU 反复读状态寄存器，直到硬件就绪。它简单可靠，代价是阻塞 CPU。在 v0.3 中，UART 输出仍然采用轮询（因为串口速度远低于 CPU，等待时间极短）；而第 12 章的 `console_readline` 在用户态读取输入时同样用轮询。中断驱动的 UART 是后续优化的方向，但轮询模型足以支撑本书全部实验。

```c
// include/uart.h
#ifndef UART_H
#define UART_H

// QEMU virt 平台 PL011 UART 基地址
#define UART0_BASE  0x09000000

// 寄存器偏移
#define UART_DR     0x00
#define UART_FR     0x18

// 标志寄存器位
#define UART_FR_TXFF    (1 << 5)
#define UART_FR_RXFE    (1 << 4)

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_puthex(unsigned int v);
void uart_puthex64(unsigned long v);
void uart_puthex_byte(unsigned char v);
char uart_getc(void);

#endif
```

```c
// src/driver/uart.c
#include "uart.h"

// 读取寄存器
#define UART_READ(reg) \
    (*(volatile unsigned int *)(UART0_BASE + (reg)))

// 写入寄存器
#define UART_WRITE(reg, val) \
    (*(volatile unsigned int *)(UART0_BASE + (reg)) = (val))

void uart_init(void) {
    // QEMU 的 UART 已经配置好，不需要额外初始化
    // 真实硬件上需要设置波特率、数据位、停止位等
}

void uart_putc(char c) {
    // 等待发送 FIFO 不满
    while (UART_READ(UART_FR) & UART_FR_TXFF) {
        // 忙等待
    }
    // 写入数据寄存器，发送字符
    UART_WRITE(UART_DR, (unsigned int)c);
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_puthex(unsigned int v) {
    const char *hex = "0123456789abcdef";
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(v >> i) & 0xF]);
    }
}

void uart_puthex64(unsigned long v) {
    const char *hex = "0123456789abcdef";
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(v >> i) & 0xF]);
    }
}

void uart_puthex_byte(unsigned char v) {
    const char *hex = "0123456789abcdef";
    uart_putc(hex[(v >> 4) & 0xF]);
    uart_putc(hex[v & 0xF]);
}

char uart_getc(void) {
    // 等待接收 FIFO 非空
    while (UART_READ(UART_FR) & UART_FR_RXFE) {
        // 忙等待
    }
    // 读取数据寄存器
    return (char)UART_READ(UART_DR);
}
```

**用宏而不是函数。**

`UART_READ`/`UART_WRITE` 是宏，编译后直接展开为一次 volatile 内存访问。宏保持内联，避免函数调用开销，也避免编译器把 volatile 访问合并。C 里实现"对某地址的读写"这类超轻量操作，宏是最直接的表达；如果写成函数，编译器在 -O0 下不会内联，每次访问都多一次调用-返回的开销和栈操作。在内核的热路径（比如发送每个字符）上，这一点差异是真实的。

**为什么 volatile。**

编译器默认会优化重复读取同一地址的代码。对 MMIO 寄存器来说，每次读写都可能改变硬件状态，必须用 `volatile` 告诉编译器"不要优化这里"。更准确地说，volatile 对编译器施加了两条约束：第一，访问不能被删除或合并；第二，访问顺序不能被重排。这两条正是寄存器语义所要求的。注意 volatile 与并发无关——它不提供原子性，多核同时访问同一寄存器仍需锁（第 13 章）。

**uart_puthex64 是 v0.3 新增。**

64 位地址（如 0x4010000000 的 PCIe ECAM）需要 16 位十六进制显示，旧的 `uart_puthex` 只支持 32 位。网络和异常调试中大量用到它。实现上两个函数完全对称：`uart_puthex` 从最高位每 4 位取一个十六进制数字输出（8 个数字），`uart_puthex64` 从第 60 位开始（16 个数字）。这种"从高到低逐 nibble 输出"的模式在内核调试打印里非常常见，值得记住。

**uart_init 是空的。**

QEMU 默认把 PL011 配置为 115200 8N1，可以直接用。真实硬件上需要设置波特率分频器（IBRD/FBRD）、控制寄存器（UARTCR）等。留空是为了教学聚焦，注释里写明了原因。这也体现了裸机开发的一个原则：**不要写你不需要的初始化**——在虚拟平台上，多余的寄存器配置反而可能引入错误。

## 四、最小 main.c

裸机程序的入口是汇编启动代码（第 5 章详细介绍），它最终会调用 C 函数 `kernel_main`。第四章的最小版本只需要输出字符串：

```c
// src/kernel/main.c
#include "uart.h"

void kernel_main(void) {
    uart_init();
    uart_puts("Hello, MyOS!\n");
    uart_puts("Chapter 4: UART Hello World\n");
    while (1) { }
}
```

注意 `while (1) { }`：`kernel_main` 返回后没有操作系统可以接管，CPU 会执行到未知地址。所以主函数必须是一个无限循环。这是所有裸机程序的共同约定。

> **教学说明**：这是第四章的**最小版 main.c**，只演示串口输出。最终版 `kernel_main`（v0.3）在此基础上增加了完整初始化链：`exception_init → irq_init → page_alloc_init → slab_init → proc_init → syscall_init → device_init → vfs_init → net_init → create_test_processes → timer_init`，最后进入 idle 循环（第 11、17 章）。

## 五、Makefile：交叉编译裸机程序

第四章的 Makefile 已经包含完整内核构建的雏形。核心是用 `aarch64-linux-gnu-` 前缀的交叉工具链，加上裸机编译选项：

```makefile
# 工具链前缀
CROSS = aarch64-linux-gnu-

CC = $(CROSS)gcc
LD = $(CROSS)ld

CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles \
         -mgeneral-regs-only -Iinclude -g -O0 -fno-pic -fno-pie
```

这些选项的含义如下。

`-ffreestanding` 告诉编译器这是裸机环境、没有标准库，允许使用不依赖宿主环境的语言特性；它同时抑制了对 `main` 等宿主约定的依赖。`-nostdlib -nostartfiles` 则不链接标准库和启动文件（crt0）——没有操作系统提供这些，任何对 libc 的隐式依赖都会在链接期暴露成 undefined reference，这本身就是一层很好的"裸机正确性"检查。`-mgeneral-regs-only` 禁止编译器使用浮点和 SIMD 寄存器：内核异常处理代码没有保存它们，一旦编译器在中断路径里用了浮点寄存器，现场恢复时就会被破坏。

`-O0` 是本书调试中得到的硬结论：必须用 -O0。早期版本用 -O2 时，编译器对 volatile 访问、异常帧布局的优化让中断路径出现难以排查的寄存器/栈问题；而 -O0 让每一步都可预测、可调试。对教学内核来说，性能不是目标，确定性才是。`-fno-pic -fno-pie` 禁用位置无关代码：裸机环境没有动态加载器，而且 v0.3 的用户程序需要"可搬移"的特性是用 PC 相对寻址实现的（第 3 章、第 8 章），与 PIC 无关。

编译运行：

```bash
cd code/ch04
make
make run
```

QEMU 启动后串口会输出：

```
Hello, MyOS!
Chapter 4: UART Hello World
```

构建系统的两个细节值得说明。第一，`make` 默认目标是 `kernel.elf`（`aarch64-linux-gnu-ld -T linker.ld` 链接），同时用 `objcopy` 生成 `kernel.bin` 裸二进制。QEMU 的 `-kernel` 参数能直接吃 ELF，所以调试期用 `kernel.elf`（保留符号表，GDB 可看符号），需要纯二进制镜像时用 `kernel.bin`。第二，v0.3 的完整 Makefile 在 `-O0` 之外还带 `-fno-pic -fno-pie`，这是为后续用户程序搬移准备的（第 8 章、第 16 章）。

## 六、调试手段：GDB 与 QEMU

裸机程序没有操作系统，自然也没有 `printf` 之外的调试手段。但 QEMU 提供了一个强大的能力：通过 GDB 远程调试。启动 QEMU 时加 `-s -S` 两个参数，`-s` 打开 1234 端口的 GDB 服务，`-S` 让 CPU 在启动前暂停：

```bash
qemu-system-aarch64 -M virt -m 256M -nographic -cpu cortex-a57 -kernel kernel.elf -s -S
```

然后在另一个终端用交叉工具链自带的 GDB 连接：

```bash
aarch64-linux-gnu-gdb kernel.elf
(gdb) target remote :1234
(gdb) info registers        # 查看所有寄存器
(gdb) x/8x 0x09000000       # 查看 UART 寄存器内容
(gdb) break kernel_main     # 在 C 函数下断点
(gdb) continue
```

在 `kernel_main` 断点命中后，`next` 单步执行，观察 `uart_puts` 逐字符写入 `UART_DR` 的过程——这是理解 MMIO 最直观的方式：你会亲眼看到每个字符以 volatile 写的形式出现在物理地址 0x09000000 上。

v0.3 的 Makefile 里专门提供了 `debug` 目标（`-s -S`）和 `run-bin` 目标（`-device loader,addr=0x40080000,kernel=kernel.bin`），第 17 章综合实验会用到。第 2 章第十节还有更完整的 GDB 实战指南。

## 七、为什么要先做 Hello World

这一章虽然简单，但它完成了三件重要的事。

第一是**验证工具链**：交叉编译、链接、QEMU 加载整条链路通畅。别小看这一步——ARM64 交叉工具链的安装、链接脚本的组织、QEMU 启动参数的组合，任何一个环节出错都会让内核"毫无反应"，而排查这类环境问题正是裸机开发的第一课。第二是**建立调试手段**：UART 是整个内核开发期间最重要的调试输出通道，之后每一章的进度打印、异常打印都依赖它。没有 UART，后面的异常处理、调度、网络栈全都无从观测。第三是**熟悉内存映射**：MMIO 的 volatile 访问模式是之后所有驱动（GIC、定时器、virtio-net）的共同基础。你在这个最简单的例子里理解透"基地址 + 偏移 + volatile 读写"，后面写任何驱动都不会再困惑。

## 八、小结

本章实现了五个里程碑：QEMU virt 平台的硬件地址认知（UART/GIC/定时器等 MMIO 基址）、PL011 UART 驱动（轮询发送、阻塞接收、32/64 位十六进制输出）、裸机编译选项与 Makefile 的完整组装、GDB 远程调试方法（-s -S + target remote）、以及第一个可运行的内核雏形。这五件事共同构成后续所有章节的开发底座：地址认知让你看得懂设备寄存器，UART 提供唯一可靠的观测通道，编译与调试环境决定你能否快速定位问题。
下一步，我们要弄清楚"CPU 为什么能跑到 `kernel_main`"——这就是第 5 章的启动代码与链接脚本。

## 九、练习

练习一，修改 `uart_puthex` 输出 8 位十六进制数，观察 0x40080000 的输出——这是理解"地址即数值"的最直接练习。练习二，给 `uart_putc` 增加换行处理：遇到 `\n` 自动补 `\r`——提示：这正好是 v0.3 第 12 章 `console_write` 要做的事，它会先输出 `\r` 再输出 `\n`，因为终端需要"回车+换行"两个字符才能回到行首下一行。练习三，把 `-O0` 改成 `-O2` 重新编译运行，观察行为差异并思考为什么裸机内核教学用 `-O0` 更稳妥——这会是全书第一个"优化与调试"的权衡实验。练习四，用 GDB 在 `uart_putc` 下断点，`x/wx` 观察 `UART_FR` 和 `UART_DR` 的值随每次写入的变化，亲手确认 volatile 访问的字节流形态。
