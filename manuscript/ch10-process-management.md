# 第十章 进程管理与上下文切换

## 一、概述

进程是操作系统最核心的概念之一。一个进程就是一个正在运行的程序实例，它有自己的寄存器状态、栈、优先级和状态。操作系统通过快速切换 CPU 在不同进程之间执行，让多个进程"同时"运行，这就是多任务。

先厘清一组容易混淆的概念。**进程（process）**是正在运行的程序实例，拥有独立的地址空间、寄存器状态与生命周期；**线程（thread）**是进程内的执行流，共享地址空间；**内核线程（kernel thread）**是运行在内核态、没有用户空间的执行流（v0.3 的 idle 和早期版本的测试线程就是）。本书的教学内核没有实现多线程（每个进程单执行流），所以"进程 == 执行流"；理解了单执行流的上下文切换，多线程只是"同一个进程内的多个 context 轮流切"的推广——上下文切换代码完全复用。

多任务的核心矛盾是：一个 CPU 在任一时刻只能执行一条指令流，如何让多个进程"看起来"同时运行？ 答案就是时间分片 + 快速切换。v0.3 采用抢占式调度：定时器每 tick 打断当前进程，调度器检查是否有更高优先级的就绪进程，有则切换（第 11 章）。本章先把"进程是什么、怎么创建、怎么切换"的机制搭好，调度策略是第 11 章的事。

v0.3 的进程管理相比早期版本做了大幅重构：引入**优先级**、**抢占式调度**、**fork/execve/wait 多进程模型**、用户进程槽位机制和僵尸进程回收。这一章我们先搭建进程管理的基础：进程控制块（PCB）、进程状态机、上下文切换、内核线程与用户进程的创建，以及 fork/execve 的实现。调度策略细节放在第 11 章。

本章代码对应最终版本 `src/proc/proc.c`、`include/proc.h`、`src/proc/context_switch.S`。


本章代码量不大（约 300 行），但它是全书机制最密集的一章：进程表、状态机、汇编切换、异常帧伪造、槽位搬移、fork 语义在一个文件里交织。建议读者按"先数据结构（PCB）、再切换机制（context）、再创建路径（线程/进程）、最后复制与替换（fork/execve）"的顺序阅读，每一节都能独立验证。
## 二、进程控制块（PCB）

进程控制块描述一个进程的所有信息。v0.3 的 `task_struct`：

```c
// include/proc.h
#ifndef PROC_H
#define PROC_H

#include <stddef.h>
#include "exception.h"

#define MAX_PROCS 64
#define PROC_NAME_LEN 32

// 用户程序加载区域：位于物理页分配器管理的 128MB（0x40000000~0x48000000）
// 之外、256MB RAM 之内，避免与内核页分配器冲突。
// 每个用户进程占一个 1MB 槽位：低处放代码，栈从槽顶向下生长。
#define USER_REGION_BASE 0x48000000UL
#define USER_SLOT_SIZE   0x100000UL
#define USER_SLOTS       8
#define USER_PROG_MAX    0xF0000UL

// 进程状态
enum proc_state {
    PROC_UNUSED = 0,
    PROC_RUNNING,
    PROC_READY,
    PROC_SLEEPING,
    PROC_ZOMBIE,
};

// CPU 上下文：callee-saved 寄存器 + 返回地址 + 栈指针
struct cpu_context {
    unsigned long x19;
    unsigned long x20;
    unsigned long x21;
    unsigned long x22;
    unsigned long x23;
    unsigned long x24;
    unsigned long x25;
    unsigned long x26;
    unsigned long x27;
    unsigned long x28;
    unsigned long x29;
    unsigned long x30;
    unsigned long sp;
};

// 进程控制块
struct task_struct {
    int pid;
    int parent_pid;
    char name[PROC_NAME_LEN];
    enum proc_state state;
    int priority;                  // 数值越小优先级越高
    unsigned long stack;           // 内核栈页（回收时释放）
    unsigned long code_base;       // 用户程序加载地址（0 表示内核线程）
    unsigned long prog_size;       // 用户程序大小（fork 时拷贝）
    struct cpu_context context;
    unsigned long wakeup_tick;     // SLEEPING：到期 tick；~0 表示等信号量
    struct task_struct *wait_next; // 等待队列链接
    int exit_code;
};
```

关键字段：

| 字段 | 含义 |
|---|---|
| `pid` / `parent_pid` | 进程号 / 父进程号（`sys_wait` 和僵尸回收用） |
| `state` | 五态：UNUSED/RUNNING/READY/SLEEPING/ZOMBIE |
| `priority` | **数值越小优先级越高**（v0.3 抢占调度核心） |
| `stack` | 内核栈页地址（每进程一页 4KB） |
| `code_base` / `prog_size` | 用户程序槽位地址与大小（内核线程为 0） |
| `context` | 上下文切换的 CPU 现场（callee-saved + sp + x30） |
| `wakeup_tick` | 睡眠唤醒时刻（tick 计数） |
| `wait_next` | 信号量等待队列链表 |
| `exit_code` | 退出码（僵尸回收/`sys_wait` 读取） |


把 `task_struct` 与 Linux 的 `struct task_struct` 对照，能立刻看出哪些字段是"教学最小集"、哪些是生产必需：Linux 的 task_struct 有几百个字段（调度域、信号、mm、fs、cgroups、审计……），v0.3 只保留 13 个，每个都对应一个真实机制——`priority` 对应 Linux 的 `prio`/`static_prio`，`wakeup_tick` 对应挂起的唤醒定时，`wait_next` 对应 wait_queue 链表节点，`exit_code` 对应 `exit_code` 字段本身。读真实内核时，按"这个字段服务哪个机制"反查，比逐字段背记高效得多。

静态数组 `proc_table[MAX_PROCS]` 而不是链表的另一个好处是**指针稳定性**：`&proc_table[i]` 永不失效（链表节点会随分配释放移动），信号量等待队列、`current` 指针、父子关系里到处都存着 `task_struct *`，静态数组保证这些指针在进程整个生命周期内始终有效——动态分配 + 复用会引入悬垂指针风险。第 13 章信号量的 `sem->wait` 链表正是依赖这一点。
## 三、进程状态机

```
            create_kernel_thread / create_user_process
                        │
                        ▼
         ┌───►  READY  ──── 调度 ────►  RUNNING
         │         ▲                      │    │
         │         │                      │    ├── 时钟抢占 ──► READY
         │         │                      │    └── sys_sleep ─► SLEEPING
         │         │                      │
         │         │                      ▼
         │       (睡眠到期/信号量唤醒)    ZOMBIE ── sys_wait/proc_reap ──► UNUSED
         └─────────┘
```

四种活跃状态各有明确含义：RUNNING 是当前在 CPU 上运行的进程（全局 `current` 指向它）；READY 是就绪等待调度的进程（数值小的优先级先被选中）；SLEEPING 是睡眠中的进程（`sys_sleep` 定时唤醒，或等信号量时 `wakeup_tick = ~0`）；ZOMBIE 是已退出但还没被回收的进程（`exit_code` 等待父进程读取）。加上未分配时的 UNUSED，就是完整的五态。

进程表是静态数组 `proc_table[MAX_PROCS]`（MAX_PROCS = 64），0 号进程是 idle，1 号起是普通进程。用静态数组而非链表，简单且便于调试（`ps` 直接遍历打印）。


状态机的五种状态覆盖了一个进程的全部生命周期。补充几个迁移细节：READY → RUNNING 由调度器完成（`pick_next` 选中后置 RUNNING），RUNNING → READY 有两条路——时钟抢占（`preempt_from_frame`，进程"被迫"让出）与主动让出之外的普通返回；RUNNING → SLEEPING 是主动睡眠（`sys_sleep` 或信号量等待），SLEEPING → READY 由唤醒者完成（`proc_tick` 时间唤醒或 `sema_up` 显式唤醒）；ZOMBIE → UNUSED 是回收（`sys_wait` 或 `proc_reap`），UNUSED → READY 是创建。SLEEPING 有两个子语义：`wakeup_tick` 有具体值时是定时睡眠（第 11 章 `proc_tick` 唤醒），等于 `~0` 时是信号量等待（第 13 章 `sema_up` 显式唤醒）——同一个状态，两种唤醒源，由 `wakeup_tick` 的值区分。
## 四、上下文切换

### （一） context_switch.S

```asm
// src/proc/context_switch.S
// 上下文切换：保存/恢复 callee-saved 寄存器与栈指针
.global context_switch
.global switch_to

// void context_switch(struct cpu_context *old, struct cpu_context *new)
// x0 = old context, x1 = new context
context_switch:
    // 保存当前进程的寄存器到 old context
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x2, sp
    str x2, [x0, #96]

    mov x0, x1
    // 落入 switch_to 恢复新进程

// void switch_to(struct cpu_context *new)
// 只恢复不保存：用于抢占路径（现场已写入 prev->context）
switch_to:
    ldp x19, x20, [x0, #0]
    ldp x21, x22, [x0, #16]
    ldp x23, x24, [x0, #32]
    ldp x25, x26, [x0, #48]
    ldp x27, x28, [x0, #64]
    ldp x29, x30, [x0, #80]
    ldr x2, [x0, #96]
    mov sp, x2
    ret
```

为什么只保存 x19-x30 和 sp？因为 ARM64 有明确的调用约定（AAPCS64）：x19-x29 是 callee-saved（被调用者必须保存），x0-x18 是 caller-saved（调用者自己保存）。上下文切换发生在"某个进程调用 `context_switch` 之后"，此时 x0-x18 已经在调用者的栈帧里，恢复进程时通过 `ret` 回到它的调用点，x0-x18 自然恢复。只有 x19-x30 和 sp 是跨切换必须保存的。


这套"只保存 callee-saved"的方案还有一层微妙处：x30（链接寄存器）既不是纯粹的 callee-saved，又是切换的关键。`context_switch` 保存 x30 是因为切换本质是"借道返回"——`switch_to` 恢复新进程的 x30 后 `ret` 跳转，x30 里装的是"新进程当初被切走时的返回地址"。对主动调度，那是 `context_switch` 的调用点（进程醒来从 `schedule()` 里 `context_switch` 返回继续）；对抢占路径，则是 `interrupt_return`（第 6 章），`ret` 到它之后从异常帧 `eret` 回到用户态。**x30 是"我该从哪里继续"的路标**，这就是为什么两种切换路径都要精心设置它。

AAPCS64 的完整寄存器分类值得画成一张表：x0-x7 传参/返回值、x8 间接结果地址、x9-x15 临时、x16-x17 是 intra-procedure-call scratch（PLT 跳板用）、x18 平台保留、x19-x29 被调用者保存（x29 常作帧指针）、x30 链接寄存器、SP 栈指针、PC 程序计数。只有 x19-x30+SP 需要跨函数边界保存，其余要么随调用栈自动保存，要么用后即弃。理解这张表，读任何 ARM64 汇编都不再困难。
### （二） 两条切换路径

v0.3 有两条切换路径：

1. **主动调度 `schedule()`**：进程主动让出 CPU（退出、睡眠、等信号量）。调用 `context_switch(&prev->context, &next->context)`，保存当前现场到 prev，恢复 next 的现场。next 再次被调度时从它的 `context_switch` 返回点继续。

2. **抢占切换 `preempt_from_frame()`**：时钟中断发现更高优先级进程就绪，把当前进程的**异常帧现场**存进 prev->context，然后 `switch_to(&next->context)` 直接恢复 next。prev 的"恢复现场"其实是 `interrupt_return`（第 6 章）——它的 context.x30 指向 `interrupt_return`，恢复后从异常帧 `eret`，如同被中断后正常返回。

```c
// 时钟中断抢占：把中断现场写入 prev 的上下文后切换。
// prev 再次被调度时经 interrupt_return 从异常帧 eret 恢复。
void preempt_from_frame(struct exception_frame *frame) {
    struct task_struct *next = pick_next();
    if (!next || next == current) return;
    if (next->priority > current->priority) return;   // 不抢占更低优先级

    struct task_struct *prev = current;
    prev->context.x19 = frame->x19;
    prev->context.x20 = frame->x20;
    prev->context.x21 = frame->x21;
    prev->context.x22 = frame->x22;
    prev->context.x23 = frame->x23;
    prev->context.x24 = frame->x24;
    prev->context.x25 = frame->x25;
    prev->context.x26 = frame->x26;
    prev->context.x27 = frame->x27;
    prev->context.x28 = frame->x28;
    prev->context.x29 = frame->x29;
    prev->context.x30 = (unsigned long)interrupt_return;
    prev->context.sp  = (unsigned long)frame;

    next->state = PROC_RUNNING;
    current = next;
    switch_to(&next->context);
}
```

## 五、创建内核线程

```c
// 内核线程：切换后从 entry 开始执行（EL1）
struct task_struct *create_kernel_thread(const char *name, void (*entry)(void), int priority) {
    struct task_struct *p = alloc_task_slot();
    if (!p) return NULL;
    void *stack = alloc_page();
    if (!stack) return NULL;

    setup_task(p, name, priority);
    p->stack = (unsigned long)stack;
    p->state = PROC_READY;

    memset(&p->context, 0, sizeof(p->context));
    p->context.sp = (unsigned long)stack + PAGE_SIZE;
    p->context.x30 = (unsigned long)entry;
    return p;
}
```

内核线程运行在 EL1，没有用户程序（`code_base = 0`）。它被调度时：`switch_to` 恢复 context（sp = 新内核栈顶，x30 = entry 函数地址），`ret` 后从 `entry` 开始执行。

## 六、创建用户进程

用户进程与内核线程的关键区别：**从 EL0 运行，通过伪造异常帧 + `interrupt_return` 进入用户态**。

```c
// 用户进程：程序拷贝到自己的槽位，在内核栈顶伪造异常帧，eret 进入 EL0
struct task_struct *create_user_process(const char *name, const void *data,
                                        unsigned long size, int priority) {
    if (!data || size == 0 || size > USER_PROG_MAX) return NULL;

    struct task_struct *p = alloc_task_slot();
    if (!p) return NULL;
    void *stack = alloc_page();
    if (!stack) return NULL;

    int slot = (int)((p - proc_table) % USER_SLOTS);
    unsigned long base = USER_REGION_BASE + (unsigned long)slot * USER_SLOT_SIZE;
    memcpy((void *)base, data, size);

    setup_task(p, name, priority);
    p->stack = (unsigned long)stack;
    p->code_base = base;
    p->prog_size = size;
    p->state = PROC_READY;

    struct exception_frame *frame = (struct exception_frame *)
        ((unsigned long)stack + PAGE_SIZE - sizeof(struct exception_frame));
    memset(frame, 0, sizeof(*frame));
    frame->elr_el1 = base;               // 入口 PC = 槽位基址
    frame->spsr_el1 = 0;                 // EL0t，中断开启


`spsr_el1 = 0` 这个赋值值得拆解。SPSR 的低 4 位是异常级别与栈选择（M[3:0]）：值 0 表示 EL0t（异常级别 0、使用 SP_EL0），这是用户态的标准运行模式。其他位：D/A/I/F（bit 9/8/7/6）是调试/SError/IRQ/FIQ 屏蔽位，0 表示全部开启——所以注释写"中断开启"。`eret` 时 CPU 用这个 SPSR 恢复 PSTATE，如果这里错误地设了 `0x3c5`（第 5 章内核自己从 EL2 降级用的值，含 EL1h 与各种屏蔽），用户进程会被放进 EL1 而不是 EL0——"用户程序以内核特权运行"是最严重的权限漏洞。`frame->elr_el1 = base` 同理：eret 的目标地址必须是用户代码。伪造异常帧的本质就是**伪造一个"刚刚发生了 EL0 → EL1 异常"的现场**，让 `eret` 反向走一遍。    frame->sp_el0 = base + USER_SLOT_SIZE; // 用户栈顶

    memset(&p->context, 0, sizeof(p->context));
    p->context.sp = (unsigned long)frame;      // 栈顶伪造异常帧
    p->context.x30 = (unsigned long)interrupt_return;
    return p;
}
```

关键机制有三步。第一步槽位选择：`(p - proc_table) % USER_SLOTS` 按进程表下标取模，把用户程序拷贝到槽位基址 `base`。第二步伪造异常帧：在内核栈顶放一个 `exception_frame`，PC（`elr_el1`）= 程序入口，状态（`spsr_el1`）= 0（EL0t），用户栈（`sp_el0`）= 槽位顶。第三步首次调度：`switch_to` 恢复 x30 = `interrupt_return`，`ret` 到 `interrupt_return` 执行 `restore_all` + `eret`，CPU 从 EL1 降到 EL0、从用户程序入口开始执行——这就是"进程第一次运行的魔法"。


整套机制的关键洞察是：**用户进程的第一次运行，与抢占恢复是同一代码路径**。`interrupt_return` 就是 `restore_all` 的别名（第 6 章），无论进程是被时钟抢占后恢复，还是刚创建第一次上 CPU，都走"恢复异常帧 → eret"这条路。区别只在异常帧的内容：抢占恢复用的是真实保存的现场，首次运行用的是伪造的现场（PC=入口、SP=槽顶、状态=EL0t）。这种"初始化 = 伪造一个符合约定的状态"是内核的通用手法——Linux 的 `copy_thread` 同样往新内核栈里摆一套寄存器/pt_regs，让首跑与恢复共用 `ret_from_fork` 路径。
## 七、fork：复制进程

`sys_fork` 是用户程序创建子进程的唯一途径（shell 的 `run` 命令用它）。v0.3 的 fork 实现：

```c
// fork：复制父进程程序到子进程槽位，复制异常帧和已用的用户栈，子进程返回 0。
// 用户代码用 ADRP 等 PC 相对寻址，拷贝到对齐的槽位后可正确运行。
long proc_fork(struct exception_frame *frame) {
    if (!current->code_base) return -1;   // 只支持用户进程

    struct task_struct *p = alloc_task_slot();
    if (!p) return -1;
    void *stack = alloc_page();
    if (!stack) return -1;

    int slot = (int)((p - proc_table) % USER_SLOTS);
    unsigned long base = USER_REGION_BASE + (unsigned long)slot * USER_SLOT_SIZE;
    memcpy((void *)base, (void *)current->code_base, current->prog_size);

    // 复制父进程已使用的用户栈（从 sp 到槽顶）
    unsigned long top = current->code_base + USER_SLOT_SIZE;
    unsigned long sp = frame->sp_el0;
    if (sp < current->code_base || sp > top) sp = top;
    unsigned long used = top - sp;
    if (used > USER_SLOT_SIZE / 2) used = USER_SLOT_SIZE / 2;
    if (used)
        memcpy((void *)(base + USER_SLOT_SIZE - used),
               (void *)(top - used), used);

    struct exception_frame *cframe = (struct exception_frame *)
        ((unsigned long)stack + PAGE_SIZE - sizeof(struct exception_frame));
    memcpy(cframe, frame, sizeof(*cframe));

    unsigned long delta = base - current->code_base;
    cframe->elr_el1 += delta;
    cframe->sp_el0  += delta;
    cframe->x0 = 0;                       // 子进程 fork 返回 0

    setup_task(p, current->name, current->priority);
    p->stack = (unsigned long)stack;
    p->code_base = base;
    p->prog_size = current->prog_size;
    p->state = PROC_READY;

    memset(&p->context, 0, sizeof(p->context));
    p->context.sp = (unsigned long)cframe;
    p->context.x30 = (unsigned long)interrupt_return;
    return p->pid;
}
```

fork 的语义是"父进程的完整快照"，落在四个方面：代码——把父进程槽位内容整体拷到子进程槽位；用户栈——把父进程已使用的栈区（从 sp 到槽顶）拷到子进程对应位置；异常帧——拷贝父进程当前的异常帧并平移 PC 和 SP（`delta = 子槽位 - 父槽位`），因为子进程的程序在另一个槽位运行；返回值——子进程的 `cframe->x0 = 0`，所以 fork 在子进程返回 0、在父进程返回子进程 pid。

### （一） fork 为什么能工作：PC 相对寻址

用户程序编译时用 ADRP/ADR/BL 指令（PC 相对寻址），代码里没有绝对地址假设。所以把整个槽位搬到新地址后，指令里的相对偏移不变，程序照常运行——**只需平移 PC（elr_el1）和 SP（sp_el0）**。这就是第 8 章讲"用户程序可加载到任意槽位"的工程落地。


fork 的"完整快照"语义在 Linux 里已经被 copy-on-write（写时复制）取代：现代 fork 只复制页表并标记只读，父子进程共享物理页，首次写时才逐页复制——创建开销从"拷贝全部内存"降到"只拷页表"。v0.3 没有 MMU 和页表，只能物理拷贝全部代码和已用栈，但**语义等价**：子进程看到的是父进程的完整快照。`used > USER_SLOT_SIZE / 2` 的截断（上限半槽 0x80000）与 `USER_PROG_MAX 0xF0000` 配套（第 8 章讲过），保证栈拷贝不会越过代码区。若读者想观察"快照"效果：在 hello 里给一个变量赋值后 fork，父子进程分别修改同一逻辑变量，验证互不影响——无 MMU 下的拷贝隔离与 MMU 下的写时复制殊途同归。
## 八、execve：替换进程映像

`sys_execve` 用内嵌用户程序替换当前进程的代码：

```c
// execve：用内嵌程序替换当前进程的代码，重置入口和栈
long proc_execve(struct exception_frame *frame, const char *name) {
    if (!current->code_base || !name) return -1;

    unsigned long size = 0;
    void *data = user_prog_find(name, &size);
    if (!data) return -1;

    memcpy((void *)current->code_base, data, size);
    current->prog_size = size;
    strncpy(current->name, name, PROC_NAME_LEN - 1);
    current->name[PROC_NAME_LEN - 1] = '\0';

    frame->elr_el1 = current->code_base;
    frame->sp_el0  = current->code_base + USER_SLOT_SIZE;
    frame->x0 = 0;
    return 0;
}
```

execve 的操作分四步：程序从内嵌表（第 5 章的 embed.S）按名字找到，拷到当前槽位；重置入口 PC = 槽位基址、用户栈 = 槽位顶；进程名同步更新；`sys_execve` 成功后，异常帧返回时 CPU 从新程序入口开始执行——exec 不新建进程，只在原进程内换程序。


`proc_execve` 与 `proc_fork` 的对比最能说明两者语义：fork 复制一切（新进程、新槽位、平移后的异常帧），execve 替换一切（同一进程、同一槽位、重置的异常帧）。`execve` 的"重置"体现在三处——代码（`memcpy` 到当前槽位）、程序大小与名字（`prog_size`/`strncpy`）、异常帧入口与栈（`elr_el1 = code_base`、`sp_el0 = 槽顶`）。注意 execve 不像 fork 那样平移 PC：因为它不搬家，新程序就在当前槽位，`elr_el1` 直接指向槽位基址即可。`user_prog_find(name, &size)` 在内嵌程序表（第 5 章 embed.S）里按名字查，找不到返回 NULL → execve 返回 -1 → shell 打印 `exec: program not found`。真实内核的 exec 家族（execve/execvp/execle）语义相同、参数形式不同，v0.3 只实现最简版本。
## 九、idle 进程与进程表初始化

```c
void proc_init(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        proc_table[i].state = PROC_UNUSED;
        proc_table[i].pid = 0;
        proc_table[i].parent_pid = 0;
        proc_table[i].stack = 0;
        proc_table[i].code_base = 0;
        proc_table[i].prog_size = 0;
        proc_table[i].wait_next = NULL;
        proc_table[i].wakeup_tick = 0;
    }

    // 0 号进程：idle（优先级数值最大 = 最不优先）
    struct task_struct *idle = &proc_table[0];
    idle->pid = 0;
    idle->state = PROC_RUNNING;
    idle->priority = 1000;
    idle->stack = 0;
    strcpy(idle->name, "idle");
    current = idle;

    uart_puts("Process manager initialized\n");
}
```

idle 进程（0 号）是调度器兜底：没有任何就绪进程时 `schedule()` 回到 idle，idle 循环执行 `proc_reap()` 回收僵尸并 `wfi` 等待中断（见 `main.c`）。


`proc_init` 把 idle 的 `priority` 设为 1000（数值最大 = 最不优先），保证只要有就绪进程，`pick_next` 绝不会选到它——它只负责"无事可做时的兜底"：回收僵尸 + `wfi` 等待中断（第 11 章）。`proc_init` 里对每个表项的逐字段清零（state/pid/parent_pid/stack/code_base/prog_size/wait_next/wakeup_tick）比简单的 `memset` 更明确：每个字段的初值都是语义的一部分（比如 `wakeup_tick = 0` 表示"不被任何时间唤醒"、`wait_next = NULL` 表示不在任何等待队列）。`current = idle` 建立"当前进程"的初始值，这是第一个调度决策的基础。
## 十、小结

本章实现的六块拼图构成完整的进程管理：进程表 + 五态状态机 + 优先级字段；上下文切换（`context_switch`/`switch_to` 两条路径，只保存 callee-saved）；内核线程创建（EL1 直接运行，无用户槽位）；用户进程创建（伪造异常帧 + `interrupt_return` 进 EL0）；fork（槽位拷贝 + 异常帧平移）与 execve（映像替换）；idle 进程与槽位机制。其中"首次运行与抢占恢复同一路径""fork 快照语义""exec 换映像不换进程"三个洞察，是理解 Linux 进程管理的钥匙。

## 十一、练习

练习一，画出 `fork` 前后父子进程的槽位布局和异常帧差异——标注代码区、栈区、elr_el1、sp_el0 与 x0 五个点的变化，这是对本章机制最完整的复盘。练习二，修改 `create_user_process` 的槽位计算公式，观察 8 个进程后槽位如何复用——理解"进程表 64 项 vs 槽位 8 个"的容量错配如何被复用吸收。练习三，在 `proc_init` 后创建两个内核线程，验证 `switch_to` 路径——需要写一个 `entry` 函数让线程循环打印并主动 `schedule()`，这是对第 11 章调度器最直接的预习。
