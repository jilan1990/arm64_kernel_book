# 第五章 启动代码与链接脚本

## 一、概述

上一章我们写了一个最简单的裸机程序，其中启动代码和链接脚本都是能跑就行的版本。这一章我们要深入理解 CPU 上电后的完整执行流程，完善启动代码，处理异常级别降级，建立更健壮的 C 运行时环境。同时深入讲解链接脚本的工作原理，理解内核镜像的内存布局。

本章代码对应最终版本 `src/boot/start.S`、`linker.ld`，以及用于内嵌用户程序的 `src/boot/embed.S` 和用户程序链接脚本 `user/user_link.ld`（后者在第 16 章详述）。

先给出一条完整的时间线，让本章的内容有个全局位置：

```text
QEMU 加载 kernel.elf
    │
    ▼
CPU 上电：PC = _start，异常级别 = EL2（QEMU 默认）
    │
    ▼
start.S：检测 CurrentEL → 配置 HCR/SPSR/ELR → ERET 降级到 EL1
    │
    ▼
EL1 下：设置 SP（_stack_top）→ 清零 BSS → 保存设备树地址
    │
    ▼
跳转 C：kernel_main()（第 4 章已见雏形，v0.3 是完整初始化链）
```

整章就围绕这条链展开：为什么 QEMU 从 EL2 启动、降级怎么做、进入 C 之前必须准备好什么、链接脚本如何决定镜像布局、用户程序怎么塞进内核。

## 二、CPU 从何处开始执行

QEMU 用 `-kernel kernel.elf` 启动时，会把 ELF 镜像中可加载的段加载到链接脚本指定的地址，并把 CPU 的入口地址（`_start`）写入 `PC`（程序计数器）。QEMU virt 机器上电时 CPU 处于 **EL2**（hypervisor 模式），这是 QEMU 的默认行为。

所以启动代码要做的第一件事就是：**从 EL2 降级到 EL1（内核模式）**。EL2 比 EL1 特权更高，QEMU 加载裸机内核时把 CPU 放在了 EL2，但我们不需要 hypervisor，应该尽早降级。

## 三、start.S：降级、清 BSS、进入 C

```asm
// src/boot/start.S
// 内核入口：从 QEMU 启动，降级到 EL1，清零 BSS，调用 kernel_main

.section .text.start
.global _start

_start:
    // 第一步：从 EL2 降级到 EL1（如果当前在 EL2）
    mrs x0, CurrentEL
    cmp x0, #0x8
    b.ne .Lel1_ok

    // 配置 HCR_EL2
    ldr x0, =(1 << 31)
    msr hcr_el2, x0

    // 配置 SPSR_EL2：EL1h，屏蔽所有中断
    ldr x0, =0x3c5
    msr spsr_el2, x0

    // 设置返回地址
    adr x0, .Lel1_ok
    msr elr_el2, x0

    eret

.Lel1_ok:
    // 第二步：设置 EL1 栈指针
    ldr x0, =_stack_top
    mov sp, x0

    // 第三步：清零 BSS 段
    ldr x0, =__bss_start
    ldr x1, =__bss_end
    bl clear_bss

    // 第四步：保存 X0 中的设备树地址（如果有）
    ldr x1, =dtb_addr
    str x0, [x1]

    // 第五步：调用 C 主函数
    bl kernel_main

    // 如果返回，死循环
.Lhang:
    b .Lhang

// 清零 BSS 段
clear_bss:
    cmp x0, x1
    b.eq .Lbss_done
    str xzr, [x0], #8
    b clear_bss
.Lbss_done:
    ret

// 设备树地址变量
.section .data
.global dtb_addr
dtb_addr:
    .quad 0
```

### （一）检测当前异常级别

`mrs x0, CurrentEL` 读取当前异常级别。`CurrentEL` 的低两位编码级别：`0b00` = EL0，`0b01` = EL1，`0b10` = EL2，`0b11` = EL3。EL2 的编码是 `0b1000`（左移 2 位后是 8），所以 `cmp x0, #0x8` 判断是否在 EL2。如果在 EL1 就直接跳过降级。

为什么要"检测"而不是"假设"？因为启动环境并不唯一：QEMU 的 `-machine virt` 默认把 CPU 置于 EL2，但某些模拟器/真实固件可能把内核直接放在 EL1 甚至 EL3；如果假设 EL2 却实际在 EL1，`msr spsr_el2` 会触发 UNDEFINED 异常。检测一下让启动代码在多种环境下都能工作，这是健壮裸机代码的基本素养。

### （二）配置 HCR_EL2

`HCR_EL2`（Hypervisor Configuration Register）控制 EL2 的虚拟化行为。bit 31 是 `RW` 位：置 1 表示 EL1 及以下使用 AArch64。我们把它设为 1，确保降级后运行 64 位代码。若此位为 0，降级后的 EL1 会处于 AArch32 状态，与我们的 64 位内核完全错位——这是新手最容易踩的隐性坑之一。

### （三）SPSR_EL2 与 ERET

`ERET`（Exception Return）指令的行为：把 `SPSR_EL2` 恢复为 PSTATE，跳转到 `ELR_EL2` 指向的地址。所以我们分三步做降级：

先设置 `SPSR_EL2 = 0x3c5`。这个值的位含义值得拆开看：bit 9 为 1 表示目标异常级别是 EL1（`M[3:0]` 字段，0x3c5 的低 4 位为 0b0101 → EL1h，即 EL1 且使用 SP_EL1）；bit 4 为 1 表示栈指针选 ELx 自己的 SP_ELx（而不是 EL0 的 SP_EL0）；bits 7-6、7-6 与 DAIF 相关位为 1 表示屏蔽全部中断（D、A、I、F）。把中断先全部关掉，等内核把中断控制器和异常向量表配好之后再开（第 6 章），是启动代码的铁律。然后设置 `ELR_EL2 = .Lel1_ok` 的地址，作为降级后的返回点。最后执行 `eret`，CPU 进入 EL1、PSTATE 恢复为 0x3c5 对应的值、PC 跳到 `.Lel1_ok` 继续执行。

注意这里 SPSR 的目标级别字段之所以是"EL1h"而不是"EL1t"：`h` 表示使用 EL1 自己的栈指针 SP_EL1，`t` 表示沿用 EL0 的 SP_EL0。内核必须用自己的栈（马上就会设置），所以必须选 EL1h。这个区分在第 6 章异常帧保存里还会再次出现——`save_all` 用 `mrs x0, sp_el1` 保存的正是 SP_EL1。

### （四）栈指针与 BSS

`_stack_top` 是链接脚本末尾定义的栈顶（在内核镜像尾部预留 0x4000 = 16KB 栈空间）。`mov sp, x0` 把 EL1 的栈设为内核栈。这一步必须在调用任何 C 函数**之前**完成——C 函数的局部变量、函数调用链全部依赖栈，栈未就绪时的任何函数调用都会写进随机地址。

BSS 是未初始化数据段（`.bss`），C 标准要求它初始为 0。启动代码用 `clear_bss` 循环把 `__bss_start` 到 `__bss_end` 之间清零。**不清理 BSS 是裸机程序最常见的 bug 之一**——全局变量会带着随机初值。注意 `clear_bss` 是一个递归风格的循环（`str xzr, [x0], #8` 后直接 `b clear_bss`），每次写 8 字节，直至 `x0 == x1`。递归在这里不可怕：它不调用自身、不压栈，只是一个带循环的标签。

### （五）设备树地址

QEMU virt 加载内核时，会把设备树二进制（DTB）的物理地址放在 x0 寄存器。Linux 内核正是靠它知道硬件布局。我们的内核主要用固定地址，但为了将来支持动态硬件探测，把 x0 保存到 `dtb_addr` 全局变量。注意保存 DTB 地址必须放在清 BSS 之后（`dtb_addr` 本身在 `.data` 段，但稳妥起见先清 BSS 再保存），否则如果它恰好落在 BSS 范围会被清零。

## 四、链接脚本：定义内核布局

```ld
// linker.ld
ENTRY(_start)

SECTIONS {
    . = 0x40080000;

    .text : {
        *(.text.start)
        *(.text)
        *(.text.*)
    }

    . = ALIGN(8);
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    . = ALIGN(8);
    .data : {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
    }

    . = ALIGN(8);
    __bss_start = .;
    .bss : {
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
    }
    . = ALIGN(8);
    __bss_end = .;

    . = ALIGN(16);
    . += 0x4000;
    _stack_top = .;

    . = ALIGN(8);
    _end = .;
}
```

关键点如下。

`ENTRY(_start)` 告诉链接器入口符号是 `_start`——QEMU 从 ELF 头读取入口地址，从它开始执行。`. = 0x40080000` 把当前地址计数器设为 0x40080000，即内核镜像链接基址，所有符号地址都从这开始计算；QEMU 的 `-kernel` 恰好把这个地址作为加载地址，链接地址与加载地址一致，内核才能直接运行（若不一致则需要 `-device loader,addr=...` 指定加载位置，Makefile 的 `run-bin` 目标展示了这种用法）。

`.text` 段的书写顺序有讲究：`*(.text.start)` 放在最前面，保证 `_start` 排在镜像开头。虽然 `ENTRY(_start)` 决定了入口符号，但段内顺序决定二进制文件里指令的实际排列——启动代码必须在最前面，因为这是 CPU 上电后第一条要执行的指令。

`__bss_start`/`__bss_end` 是给启动代码用的 BSS 边界符号，链接器按 8 字节对齐计算，start.S 的 `clear_bss` 就用这两个符号圈定清零范围。最后，镜像末尾预留 0x4000（16KB）给内核栈，`_stack_top` 是栈顶（栈向下生长，所以栈顶在内核镜像的高地址端）；`_end` 是镜像真正结束的地址，页分配器（第 7 章）从这里开始找空闲页。把整张布局图画出来就是：

```text
0x40080000  ┌───────────────┐
            │ .text.start   │  ← _start（启动汇编）
            ├───────────────┤
            │ .text         │  ← 其余代码、异常向量表
            ├───────────────┤
            │ .rodata       │  ← 字符串、常量、内嵌用户程序
            ├───────────────┤
            │ .data         │  ← 已初始化全局变量
            ├───────────────┤
            │ .bss          │  ← 未初始化全局变量（清零）
            ├───────────────┤
            │ 栈区 16KB     │  ← 内核栈（向下生长）
            ├───────────────┤
0x4008xxxx  └───────────────┘  ← _end（镜像结束，页分配器起点）
```

## 五、异常向量表的放置

链接脚本中 `.text` 段包含 `start.S`（`.text.start`）、`exception.S`（`.text.vectors`）等。异常向量表要求 2KB 对齐（`exception.S` 中 `.align 11`），链接器会保证它落在 `.text` 段内合适的位置。向量表的具体内容在第 6 章讲解。

## 六、embed.S：把用户程序装进内核

v0.3 新增了 `src/boot/embed.S`，它把编译好的用户程序二进制直接内嵌进内核镜像：

```asm
# src/boot/embed.S -- 把用户程序二进制嵌入内核镜像
.section .rodata.embed

.balign 8
.global shell_prog_start
.global shell_prog_end
shell_prog_start:
.incbin "user/shell.bin"
shell_prog_end:

.balign 8
.global hello_prog_start
.global hello_prog_end
hello_prog_start:
.incbin "user/hello.bin"
hello_prog_end:

.balign 8
.global test_prog_start
.global test_prog_end
test_prog_start:
.incbin "user/test.bin"
test_prog_end:
```

### （一）为什么用 .incbin 内嵌

用户程序（shell、hello、test）编译成独立的 ELF（链接基址 0x48000000，见第 16 章），再 `objcopy` 成纯二进制 `.bin`，最后用 `.incbin` 指令把字节直接塞进内核镜像的 `.rodata.embed` 段。这样有两层好处：

一方面，用户程序随内核镜像一起加载，无需单独的磁盘或文件系统来存放——在没有块设备驱动和文件系统可用的早期，这是把用户代码带进内核最直接的办法。另一方面，每个程序的 `_start`/`_end` 符号被导出，内核通过符号名就能拿到程序的字节范围和大小，`execve` 系统调用执行时直接把这段字节复制到目标槽位（第 10 章 `proc_execve`、第 16 章 `user_prog_find`）。

### （二）内嵌表的访问

`proc.c` 里维护一张内嵌程序表：

```c
static const struct embedded_prog embedded_progs[] = {
    { "shell", shell_prog_start, shell_prog_end },
    { "hello", hello_prog_start, hello_prog_end },
    { "test",  test_prog_start,  test_prog_end  },
    { 0, 0, 0 },
};
```

`user_prog_find(name, &size)` 按名字查找，返回程序起始地址和大小。`execve` 系统调用（第 12 章）就是靠它找到要替换的程序。表尾的 `{ 0, 0, 0 }` 是哨兵项——链表/数组用特殊值标记结尾，避免传长度参数，这是内核代码里常见的惯用法。

### （三）构建依赖

Makefile 中用户程序二进制必须先于 `embed.o` 构建：

```makefile
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
```

`user/shell.elf`、`user/hello.elf`、`user/test.elf` 用 `USER_CFLAGS`/`USER_LDFLAGS` **一步直链**（C 程序不经中间 .o），C 源文件、`user_syscall.h`、`user_link.ld` 任一变化都会触发重建。`embed.o` 依赖 `$(USER_BINS)` 的意义在于构建顺序：`.incbin "user/shell.bin"` 在汇编阶段就要读这个文件，如果它还不存在，汇编直接失败。把依赖关系写清楚，`make` 会自动完成：C/汇编 → ELF → bin → 内嵌进内核镜像。

v0.3 的 Makefile 比旧版多了关键的两个变化：用户程序**不再经过中间 .o 文件**（一步直链，规则更简单、依赖更直接），并且 `user/test.elf` 走汇编路径（`test.S` 是纯汇编用户程序，不需要 C 编译器前端）。这两处是本书重写时对照 v0.3 真源逐字核对过的。

## 七、异常向量表符号与 irq.c 的对接

`exception.S` 导出 `vectors`（向量表基址），`irq.c` 的 `exception_init()` 把它写入 `vbar_el1`：

```c
extern char vectors[];  // 异常向量表（exception.S）

void exception_init(void) {
    asm volatile("msr vbar_el1, %0" :: "r"(vectors));
}
```

`VBAR_EL1`（Vector Base Address Register）指向异常向量表。设置后，任何异常（中断、SVC、数据中止）都会跳转到向量表对应入口。这一步是异常处理的基础，第 6 章详解。

值得注意：`exception_init` 在 v0.3 的 `kernel_main` 初始化链中排在**第二位**（紧随 `uart_init` 之后）——在开中断之前，异常向量表必须已经就位，否则任何异常（包括最普通的时钟中断）都会跳到一个未初始化的地址，直接导致 CPU 跑飞。而 `irq_init`（开中断）排在其后，正是依赖这个顺序保证。启动代码的"先建地基，再开设备"哲学在这里体现得淋漓尽致：栈 → BSS → 向量表 → 中断，每一层都建立在前一层之上，任何一步颠倒都会以极难排查的方式失败。

## 八、总结与调试视角

本章的启动代码是全书唯一"没有 C 环境可用"的阶段，调试手段也因此特殊。最常见的三类问题值得提前预判。其一，**BSS 清零遗漏**：`clear_bss` 循环写错边界会让未初始化的全局变量带随机值，症状是某个子系统初始化日志忽有忽无——用 GDB 在 `kernel_main` 入口检查 `_bss_start` 到 `_bss_end` 是否全零即可定位。其二，**SP 未切栈**：`msr spsel, #1` 之前代码仍在 EL2 栈（QEMU 给的少量引导栈），递归或大局部变量会立刻溢出——把栈顶地址与 `_stack_top` 打印出来对比。其三，**链接脚本与代码不一致**：`linker.ld` 里段的顺序与 `start.S` 假设不符（比如把 `.bss` 放在 `.data` 前面），会导致符号地址错位——`objdump -h kernel.elf` 查看段布局是标准核对手段。启动代码的调试原则与后面章节不同：**这里每一步都发生在"世界建立"之前，错了就是重启**，所以更依赖静态核对而非运行时修复。

## 八、小结

本章完成了四件事。

第一是**异常级别降级**：QEMU 把 CPU 放在 EL2，我们用 `HCR_EL2.RW` + `SPSR_EL2 = 0x3c5` + `ELR_EL2` + `eret` 的经典组合降级到 EL1。这一步是"从固件手中接管 CPU"的关键，也是理解异常级别切换的入门课——`eret` 是唯一能改异常级别的指令，后面的系统调用返回（第 6 章 `interrupt_return`）用的还是它。第二是**C 运行时环境**：设置 SP_EL1、清零 BSS、保存设备树地址，让 C 代码可以安全地执行。第三是**链接脚本**：理解了 `. = 0x40080000`、段顺序、BSS 边界、栈与 `_end` 的布局，以及"链接地址 == 加载地址"才能直接运行的原则。第四是**用户程序内嵌**：embed.S 用 `.incbin` 把用户程序字节塞进 `.rodata.embed`，构建依赖保证顺序，`proc.c` 用哨兵表管理三个内嵌程序。

至此，CPU 已经能在 EL1 下稳定运行 C 代码。下一步我们给内核装上"神经系统"——异常与中断处理。

## 九、练习

练习一，把链接基址改成 0x40000000 用 `make run-bin` 运行，观察差异（需要 `-device loader` 配合，且加载地址必须与链接地址一致——这直接检验你对"链接地址 == 加载地址"的理解）。练习二，在 `clear_bss` 加个打印确认 BSS 是否真的被清零：定义一个大数组全局变量，初始值若为 0 说明清零成功，若为随机值说明 BSS 未清——这是启动阶段最常见的隐性 bug 之一。练习三，给 embed.S 加第四个内嵌程序（比如 `user/echo.bin`），并在 Makefile 里补全构建规则，完整走一遍"用户程序编译 → objcopy → embed → 哨兵表"的链路。练习四，用 GDB 在 `_start` 下断点，单步观察 `CurrentEL` 从 8（EL2）变为 4（EL1）的过程，理解 `eret` 前后的 PSTATE 变化——这是全书对异常级别切换最直观的一次观察。
