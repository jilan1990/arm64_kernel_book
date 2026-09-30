# 第六章 异常与中断处理

## 一、概述

异常和中断是操作系统的核心机制。当硬件或软件需要内核立即处理某个事件时，它会触发一个异常，CPU 暂停当前执行的代码，跳转到异常处理程序。系统调用、定时器中断、外设中断、数据中止等，都是通过异常机制实现的。

理解异常模型的关键是把它看作 CPU 层面的"硬中断控制流转移"：与函数调用（BL/RET）不同，异常发生时 CPU 会自动切换异常级别、切换栈指针、保存返回地址与状态，并且**不可被普通代码绕过**——这正是操作系统获得控制权的方式。用户程序想访问内核资源、内核想响应硬件事件，都必须经由这条唯一入口。本书主线中三处最重要的机制都建立在异常之上：第 12 章的系统调用（SVC → 同步异常）、第 11 章的抢占调度（定时器 IRQ）、第 13 章的进程睡眠唤醒（同样是定时器 IRQ 驱动）。

本章代码对应最终版本 `src/boot/exception.S`、`src/kernel/irq.c`、`src/driver/gic.c` 以及头文件 `include/exception.h`、`include/gic.h`、`include/irq.h`。

## 二、AArch64 异常模型

### （一） 异常向量表

AArch64 有 4 类异常源、4 个入口，构成 16 项的向量表：

| 异常源 | 说明 |
|---|---|
| 同步异常（Synchronous） | 数据中止、指令中止、SVC、未定义指令等 |
| IRQ | 普通中断 |
| FIQ | 快速中断（比 IRQ 优先级更高） |
| SError | 系统错误（总线错误等异步错误） |

加上 4 个运行场景（当前 EL 用 SP_EL0 / 当前 EL 用 SP_ELx / 更低 EL 用 AArch64 / 更低 EL 用 AArch32），共 16 项，每项 128 字节对齐，整个向量表 2KB（2048 字节）对齐。

为什么向量表要 16 项而不是 4 项？因为"来自哪里"决定处理策略：内核态（当前 EL）的异常通常意味着内核 bug，应该打印现场后停机；用户态（更低 EL）的异常则可能可恢复（比如系统调用）。`SP_EL0` 与 `SP_ELx` 的区分同理——如果用 SP_EL0 时的异常，栈指针指向用户栈，内核不能信任它。向量表把场景差异编码在入口地址里，处理程序从"自己落在哪个入口"就能推断上下文。

与 x86 对照：x86 用 IDT（中断描述符表）配合 256 个门描述符，每项 16 字节，通过 `INT n` 指令编号触发；ARM64 则用固定 16 项 + 异常级别区分。设计上 ARM64 更强调"级别隔离"，x86 更强调"向量编号"。理解差异有助于读两种架构的真实内核代码。

### （二） 向量表宏

`exception.S` 用 `ventry` 宏生成向量表项：

```asm
// 向量表项宏：对齐到 128 字节，放一条跳转，其余填 NOP
.macro ventry handler
.align 7
    b \handler
    .rept 31
    nop
    .endr
.endm

// ========== 异常向量表 ==========
.align 11
vectors:
vectors_base:

// 当前 EL 使用 SP_EL0
ventry sync_handler
ventry irq_handler
ventry fiq_handler
ventry error_handler

// 当前 EL 使用 SP_ELx
ventry sync_handler_kern
ventry irq_handler
ventry fiq_handler
ventry error_handler

// 更低 EL 使用 AArch64（用户程序 svc/中断走这里）
ventry sync_handler
ventry irq_handler
ventry fiq_handler
ventry error_handler

// 更低 EL 使用 AArch32
ventry sync_handler
ventry irq_handler
ventry fiq_handler
ventry error_handler
```

v0.3 的改进：**当前 EL 使用 SP_ELx 的同步异常走 `sync_handler_kern`**，与用户态同步异常（走 `sync_handler`）区分入口。虽然目前两者都调用 `do_sync_handler`，但保留独立入口为将来"内核态异常必须严肃停机、用户态异常可恢复"的策略留出位置。

## 三、异常帧的保存与恢复

### （一） 异常帧结构

异常发生时，CPU 硬件只自动保存很少的信息（ELR_EL1 返回地址、SPSR_EL1 状态）。其他 31 个通用寄存器必须由软件保存。v0.3 把异常帧定义为 C 结构体：

```c
// include/exception.h
struct exception_frame {
    // 通用寄存器 x0-x30
    unsigned long x0, x1, x2, x3, x4, x5, x6, x7;
    unsigned long x8, x9, x10, x11, x12, x13, x14, x15;
    unsigned long x16, x17, x18, x19, x20, x21, x22, x23;
    unsigned long x24, x25, x26, x27, x28, x29, x30;
    // 特殊寄存器
    unsigned long sp_el0;    // 用户态栈指针
    unsigned long elr_el1;   // 异常返回地址
    unsigned long spsr_el1;  // 保存的 PSTATE
};
```

这个结构体与汇编的 `save_all`/`restore_all` 宏布局严格对应，C 代码（系统调用、进程切换）可以直接读写异常帧。

### （二） save_all：保存现场

```asm
// 保存所有通用寄存器到栈
.macro save_all
    sub sp, sp, #EXC_FRAME_SIZE
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x0, sp_el0
    str x0, [sp, #248]
    mrs x0, elr_el1
    str x0, [sp, #256]
    mrs x0, spsr_el1
    str x0, [sp, #264]
.endm
```

`EXC_FRAME_SIZE` 是 272 字节（31 个寄存器 ×8 + 3 个特殊寄存器 ×8）。注意保存顺序与 C 结构体一致：x0 在偏移 0，sp_el0 在偏移 248，elr_el1 在 256，spsr_el1 在 264。

**为什么 sp_el0 也要保存？** 异常发生时如果从 EL0 陷入，CPU 会自动切换到 SP_EL1（内核栈），用户栈指针 SP_EL0 只是被硬件"留"在寄存器里，并没有自动保存。系统调用参数可能包含用户空间指针（比如 `write(fd, buf, len)` 的 buf），内核要把数据写到用户栈，就必须知道用户栈在哪——所以 SP_EL0 必须随异常帧保存。这也解释了为什么 `create_user_process` 初始化新进程时要设置 `frame->sp_el0`：首次进入 EL0 时，`restore_all` 会从伪造的异常帧里恢复用户栈指针。

**为什么是 31 个通用寄存器而不是 32？** x0-x30 共 31 个；第 32 个"寄存器"是 SP/PC，但 SP 按 EL 分开保存（sp_el0 在帧里），PC 由 elr_el1 携带，所以帧结构就是 31 + 3 的布局。

### （三） restore_all：恢复现场（v0.3 关键修复）

```asm
// 从栈恢复所有寄存器并返回
// 注意：sp_el0/elr/spsr 用 x2 作暂存，最后再重载 x2/x3，避免破坏已恢复的用户寄存器
.macro restore_all
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    ldr x2, [sp, #248]
    msr sp_el0, x2
    ldr x2, [sp, #256]
    msr elr_el1, x2
    ldr x2, [sp, #264]
    msr spsr_el1, x2
    ldp x2, x3, [sp, #16]
    add sp, sp, #EXC_FRAME_SIZE
    eret
.endm
```

**v0.3 修复的 bug**：旧版 `restore_all` 用 `ldr x0` 依次恢复 sp_el0/elr/spsr，这会在恢复 x2/x3 之前就破坏 x0。用户态寄存器 x0-x3 是系统调用的参数/返回值，被破坏后用户程序会读到错误的值。v0.3 改用 **x2 作暂存寄存器**，先恢复完特殊寄存器，再 `ldp x2, x3, [sp, #16]` 重载用户真正的 x2/x3。

这里其实有个更深的坑值得展开：`restore_all` 的寄存器恢复顺序不能随意写。x0-x30 是"一整组"要恢复的值，但 `msr` 写入特殊寄存器会覆盖通用寄存器（`msr sp_el0, x2` 不覆盖 x2，但恢复通用寄存器的 `ldp` 会覆盖）。任何"先恢复特殊寄存器、后恢复通用寄存器"的实现都必须找一个**还没被恢复的**通用寄存器作暂存——旧版用 x0（已被恢复），v0.3 用 x2（尚未恢复）。这是汇编级 bug 的经典教材案例：单步调试时看起来"每条指令都对"，但组合起来的顺序错了，只有观察最终用户态寄存器值才能发现。

### （四） interrupt_return：从伪造异常帧返回

v0.3 新增全局符号 `interrupt_return`，它只是 `restore_all` 的一个别名入口：

```asm
// 从当前栈上的异常帧直接 eret 返回。
// 用途：新创建的进程首次被调度、以及被抢占的进程再次被调度时，
// context.sp 指向异常帧、context.x30 指向这里。
interrupt_return:
    restore_all
```

新进程第一次"运行"并不是从某条指令被中断，而是内核伪造了一个异常帧（PC = 程序入口、SP = 用户栈顶、PSTATE = EL0t），然后把上下文切换的目标指向 `interrupt_return`。这样 `restore_all + eret` 就完成了从内核态到用户态的第一跳。第 10、11 章会详细讲解这条路径。

## 四、异常分发：do_sync_handler

`irq.c` 中的 `do_sync_handler` 按 ESR（Exception Syndrome Register）的 EC 字段分发：

```c
// 同步异常：SVC 走系统调用，其余打印现场并停机
void do_sync_handler(struct exception_frame *frame) {
    unsigned long esr, far;
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    unsigned long ec = (esr >> 26) & 0x3f;

    if (ec == 0x15) {          // ESR.EC = SVC，AArch64
        handle_syscall(frame);
        return;
    }

    asm volatile("mrs %0, far_el1" : "=r"(far));
    uart_puts("\nSYNC EXCEPTION! EC=");
    uart_puthex((unsigned int)ec);
    uart_puts("\n  ESR: "); uart_puthex64(esr); uart_puts("\n");
    uart_puts("  FAR: "); uart_puthex64(far); uart_puts("\n");
    uart_puts("  ELR: "); uart_puthex64(frame->elr_el1); uart_puts("\n");
    uart_puts("  SPSR: "); uart_puthex64(frame->spsr_el1); uart_puts("\n");
    dump_frame(frame);
    while (1) { }
}
```

要点：

1. **ESR.EC = 0x15 是 SVC**：用户程序执行 `svc #0` 指令时产生同步异常，EC 字段（bits 31-26）为 0x15（`ESR_ELx_EC_SVC64`）。此时把异常帧交给 `handle_syscall` 分发（第 12 章）。
2. **其他同步异常**：打印 ESR/FAR/ELR/SPSR 和全部 31 个寄存器（`dump_frame`），然后停机 `while(1)`。这是开发期最重要的调试工具——任何内核 bug 都会在这里留下完整的现场。

## 五、GIC 中断控制器

### （一） GIC 架构

QEMU virt 平台使用 GICv2（Generic Interrupt Controller）。GIC 分两部分：

- **分发器（Distributor）**，基址 0x08000000：管理所有中断源，决定中断使能、优先级、路由。
- **CPU 接口（CPU Interface）**，基址 0x08010000：每个 CPU 一个，管理中断的应答（ACK）和结束（EOI）。

GIC 的中断按类型分三类：SGI（软件生成中断，核间通信用）、PPI（私有外设中断，每核独立）、SPI（共享外设中断，可路由到任意核）。QEMU virt 上，串口（IRQ 33）、定时器（IRQ 27）等都是 SPI。Linux 用 irq domain 抽象这套层级（GIC → 控制器 → 中断号映射），我们的内核简化成一张 256 项的 `irq_handlers` 表——规模小，线性数组反而最清晰。这一章只用了 `ISENABLER/ICENABLER/IAR/EOIR` 四个寄存器；完整的 GIC 还提供优先级配置（`GICD_IPRIORITYR`）、触发方式（`GICD_ICFGR`）等，教学内核用不到。

```c
// include/gic.h
#define GIC_DIST_BASE   0x08000000
#define GIC_CPU_BASE    0x08010000

// 分发器寄存器
#define GICD_CTLR       0x000
#define GICD_ISENABLER  0x100
#define GICD_ICENABLER  0x180
...
// CPU 接口寄存器
#define GICC_CTLR       0x000
#define GICC_PMR        0x004
#define GICC_IAR        0x00C
#define GICC_EOIR       0x010
```

### （二） GIC 驱动实现

```c
// src/driver/gic.c
void gic_init(void) {
    // 启用分发器
    GICD_WRITE(GICD_CTLR, 0x1);
    // 启用 CPU 接口
    GICC_WRITE(GICC_CTLR, 0x1);
    // 设置优先级掩码为最低优先级（允许所有中断）
    GICC_WRITE(GICC_PMR, 0xff);
}

void gic_enable_irq(int irq) {
    // 每个寄存器管理 32 个中断
    int reg = irq / 32;
    int bit = irq % 32;
    GICD_WRITE(GICD_ISENABLER + reg * 4, (1 << bit));
}

int gic_acknowledge(void) {
    return GICC_READ(GICC_IAR) & 0x3ff;  // 低 10 位是中断号
}

void gic_end_of_interrupt(int irq) {
    GICC_WRITE(GICC_EOIR, irq & 0x3ff);
}
```

中断处理的标准流程：

1. **应答（Acknowledge）**：读 `GICC_IAR`，得到中断号。这一步同时告诉 GIC"我接管了这个中断"，中断进入 active 状态。
2. **处理**：调用注册的中断处理函数。
3. **结束（EOI）**：写 `GICC_EOIR`，告诉 GIC"处理完了"，中断可以重新触发。

## 六、中断注册与分发：irq.c

### （一） 中断处理函数表

```c
static irq_handler_t irq_handlers[MAX_IRQ];   // MAX_IRQ = 256
static void *irq_data[MAX_IRQ];

int irq_register(int irq, irq_handler_t handler, void *data) {
    if (irq < 0 || irq >= MAX_IRQ) return -1;
    irq_handlers[irq] = handler;
    irq_data[irq] = data;
    gic_enable_irq(irq);
    return 0;
}
```

驱动只需调用 `irq_register(irq, handler, data)` 注册，中断使能由 `irq_register` 自动完成。

### （二） IRQ 处理入口（v0.3 的 EOI 时序修复）

```c
void irq_handler_c(struct exception_frame *frame) {
    int irq = gic_acknowledge();

    if (irq >= 1020) return;   // spurious

    if (irq < MAX_IRQ && irq_handlers[irq]) {
        irq_handlers[irq](irq, irq_data[irq]);
    } else {
        uart_puts("Unexpected IRQ: ");
        uart_puthex((unsigned int)irq);
        uart_puts("\n");
    }

    // 必须先 EOI 再抢占，否则该中断一直处于 active，后续中断无法送达
    gic_end_of_interrupt(irq);

    if (need_resched) {
        need_resched = 0;
        preempt_from_frame(frame);
    }
}
```

**v0.3 的关键修复：EOI 必须先于抢占切换完成。** 如果先切换到其他进程再 EOI，当前中断会一直处于 active 状态，GIC 认为该中断还没处理完，后续同源或同优先级中断都无法送达，导致中断丢失甚至系统假死。所以 `irq_handler_c` 严格按"ACK → 处理 → EOI → 抢占"的顺序执行。

这个 bug 在真实内核开发里非常典型：症状是"系统跑一会儿就完全卡死"，但卡死点看起来跟中断毫无关系。根因是 GIC 的 active 状态机——中断被 ACK 后进入 active，只有 EOI 才让它回到 inactive 并能再次触发。如果抢占切换让 EOI 永远不执行（被切走的进程上下文里 EOI 丢失），该中断源就永久锁死。调试手段：在 EOI 前后各打印一次中断号，观察哪个中断号"进去后没出来"。

### （三） 抢占与 need_resched

`proc_tick()`（时钟中断处理，第 11 章）会检查是否有更高优先级进程就绪，若有则置 `need_resched = 1`。IRQ 处理尾声看到这个标志后，调用 `preempt_from_frame(frame)` 把当前进程的现场（就是栈上的异常帧）保存进它的上下文，切换到下一个进程。被切换走的进程下次被调度时，会从 `interrupt_return` 恢复现场继续执行，看起来就像"被中断后正常返回"。

这里有一个值得体会的设计：**抢占点被统一放在 IRQ 尾声**。所有中断处理共享同一条"EOI → 检查 need_resched → 切换"的路径，而不是在每个驱动 handler 里各自调用 schedule。这让调度时机可预测、抢占逻辑只写一次。Linux 的内核抢占（CONFIG_PREEMPT）同样是在中断返回路径（preempt_schedule_irq）统一处理的——原理一致，只是粒度更细（还要检查 preempt_count）。

### （四） irq_init

```c
void irq_init(void) {
    gic_init();
    asm volatile("msr daifclr, #2");   // 开 IRQ
}
```

`msr daifclr, #2` 清除 DAIF 中的 I（IRQ 屏蔽）位，打开中断。注意 `daifclr #2` 只开 IRQ（bit 1），不开 FIQ（bit 0）和同步（bit 3）。

## 七、异常帧打印：dump_frame

```c
static void dump_frame(struct exception_frame *frame) {
    static const char *names[31] = {
        "x0 ", "x1 ", "x2 ", "x3 ", "x4 ", "x5 ", "x6 ", "x7 ",
        "x8 ", "x9 ", "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "x29", "x30"
    };
    unsigned long *regs = &frame->x0;
    for (int i = 0; i < 31; i++) {
        uart_puts(names[i]);
        uart_puts("=");
        uart_puthex64(regs[i]);
        uart_puts((i % 4 == 3) ? "\n" : "  ");
    }
    uart_puts("sp_el0="); uart_puthex64(frame->sp_el0); uart_puts("\n");
}
```

开发期间，任何未知同步异常都会完整打印所有寄存器，配合 GDB（第 2 章）可以精确定位出错位置。

`dump_frame` 的输出格式与 GDB 的 `info registers` 对齐，方便人工对照。它把 31 个寄存器按 4 个一组换行（`i % 4 == 3` 时输出换行），每组用两个空格分隔，保证每行宽度可读。寄存器名用固定宽度（`"x0 "` 带尾空格、`"x10"` 不带），目的就是让 `=` 号纵向对齐——调试输出对齐虽是小细节，但在几十个寄存器的现场里快速找差异值时会省大量时间。v0.3 的 `do_sync_handler` 在打印完寄存器后进入 `while (1) { }` 停机：开发内核遇未知异常应当**立即停机**，而不是尝试"恢复"——恢复一个状态不明的内核只会让错误传播得更远。

## 八、异常/中断的完整处理链

```
用户程序执行 svc #0 / 硬件产生 IRQ
        │
        ▼
CPU 切换异常级别，跳转 VBAR_EL1 指向的向量表
        │
        ▼
向量表 → save_all（保存 31 个寄存器 + sp_el0/elr/spsr 到内核栈）
        │
        ▼
sync_handler → do_sync_handler（EC=0x15 → handle_syscall）
或 irq_handler → irq_handler_c（ACK → handler → EOI → 可能抢占）
        │
        ▼
restore_all → eret 返回被中断的现场
```

本章还值得记住一个宏观结论：异常帧（exception_frame）是全书"上下文"概念的原始形态。第 10 章的 `cpu_context` 是它的精简版（只存 callee-saved），第 11 章抢占路径的 `preempt_from_frame` 直接把异常帧搬进上下文，第 12 章系统调用参数直接读异常帧——**所有的寄存器保存/恢复，最终都回到本章的 save_all/restore_all 这一对汇编函数**。理解异常帧的布局（31 通用 + sp_el0 + elr + spsr，共 272 字节）与恢复顺序（x2 暂存），就等于掌握了整本书上下文机制的地基。

## 九、小结

本章实现的六个部件构成完整的异常处理链：AArch64 异常向量表（16 项、2KB 对齐、VBAR_EL1 指向）、异常帧的保存/恢复（v0.3 修复了 restore_all 用 x0 暂存破坏用户寄存器的 bug，改用 x2）、`interrupt_return` 全局入口（为第 10 章进程首次进入 EL0 铺路）、按 ESR EC 分发的同步异常处理（SVC → 系统调用）、GICv2 驱动（使能、应答、EOI 三件套）、以及 IRQ 分发 + 抢占钩子（EOI 先于抢占的设计）。读者应能把它们与"用户程序 svc → 向量表 → save_all → handler → restore_all → eret"的完整链路一一对应。

## 十、练习

练习一，在用户态程序里故意执行 `svc #0x1234`（未定义的系统调用号），观察 `handle_syscall` 返回 -1 的行为——这验证了"未知系统调用必须安全失败而不是崩溃"的内核原则。练习二，把 `restore_all` 改回用 x0 暂存，跑 `run hello` 观察用户程序输出的异常——你会亲眼看到 v0.3 修复（改用 x2 暂存）的必要性，这是全书最值得亲自动手的"改回去"实验。练习三，给 GIC 加一个"未处理中断"计数，开机后统计有多少 `Unexpected IRQ`——理解 GIC 中断号与处理器之间可能存在的不匹配现象。
