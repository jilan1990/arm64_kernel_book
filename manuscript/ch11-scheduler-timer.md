# 第十一章 调度器与定时器

## 一、概述

上一章我们实现了进程管理的基础：进程表、上下文切换、进程创建。这一章实现调度器——决定"下一个运行谁"的策略，以及定时器——提供时间基准。


调度器在操作系统中的地位，相当于"资源分配的核心仲裁者"：CPU 是系统最宝贵的资源，谁在什么时刻获得 CPU、获得多久，由调度策略决定。v0.3 采用静态优先级抢占调度，这是教学内核的主流选择：每个进程在创建时被赋予一个固定优先级，数值越小优先级越高，调度器在每个时钟 tick 检查是否有更优进程，有则立即切换。真实内核的调度器远比这复杂——Linux 的 CFS（完全公平调度器）不显式分配优先级，而是用红黑树按"虚拟运行时间"排序，追求所有进程公平分享 CPU；RT（实时）调度类则像我们的优先级抢占，保证高优先级任务即时执行。理解 v0.3 的"数值优先级 + tick 抢占"，再去看 CFS 的"公平时间片 + 惰性切换"，就抓住了两种调度范式的分界：优先级是显式策略，公平是隐式策略。
v0.3 的调度器是**优先级抢占式**的：数值越小优先级越高，时钟中断每个 tick 检查是否有更高优先级进程就绪，有则抢占当前进程。同时实现睡眠唤醒（`sys_sleep` 到期自动醒来）和僵尸回收（`proc_reap`）。

本章代码对应最终版本 `src/proc/proc.c`（调度部分）、`src/driver/timer.c`、`include/timer.h`、`src/kernel/irq.c`（抢占钩子）。

## 二、调度策略：静态优先级抢占

### （一） pick_next

调度器的核心是"选下一个进程"。v0.3 用最简单的**静态优先级**：遍历进程表，选出就绪进程中优先级数值最小（最高）的那个。

```c
// 选择就绪进程中优先级最高（数值最小）的
static struct task_struct *pick_next(void) {
    struct task_struct *best = NULL;
    for (int i = 1; i < MAX_PROCS; i++) {
        struct task_struct *p = &proc_table[i];
        if (p->state == PROC_READY && (!best || p->priority < best->priority))
            best = p;
    }
    return best;
}
```

**抢占规则**（在 `preempt_from_frame` 中）：只有 `next->priority <= current->priority` 才抢占。同优先级不抢占（避免两个同优进程互相抢，导致某个进程饿死）。


这里有一个值得琢磨的设计决策：**为什么同优先级不抢占？** 如果允许同优先级互相抢占，两个同优进程会在每个 tick 疯狂交换 CPU——每次切换都要保存/恢复全部寄存器、冲刷流水线，CPU 大量时间花在切换上而不是执行上，这就是"抖动"（thrashing）。只在高优先级进程就绪时才抢占，同优先级进程自然轮流获得完整 tick，既保证了"优先级高者先跑"，又避免了无意义的频繁切换。代价是低优先级进程可能长期得不到 CPU（饥饿），但 v0.3 的负载简单（shell + 短暂的用户程序），优先级设计足以覆盖。若读者想防饥饿，可以给进程加"老化"（ageing）机制：等待时间越久优先级数值越小——这已经是真实调度器的常见手段，留作练习。

`pick_next` 是 O(n) 的线性扫描（n=64 个进程表项），每次调度都要扫一遍。对教学内核完全够用；Linux 的 O(1) 调度器曾用 140 个优先级桶数组做到常数时间选择，CFS 用红黑树做到对数时间。这里的演进路径值得记住：**数据结构的复杂度，永远跟着调度策略的需求走**。
```c
if (next->priority > current->priority) return;   // 不抢占更低优先级
```

### （二） 主动调度 schedule

```c
// 内核态主动调度（进程退出、睡眠等场景）
void schedule(void) {
    struct task_struct *next = pick_next();
    if (!next) next = &proc_table[0];   // 无就绪进程则回 idle
    if (next == current) return;

    struct task_struct *prev = current;
    next->state = PROC_RUNNING;
    current = next;
    context_switch(&prev->context, &next->context);
}
```

调用场景：进程 `sys_exit`（转为 ZOMBIE 后让出）、`sys_sleep`（转为 SLEEPING 后让出）、信号量 `sema_down`（加入等待队列后让出）。这些都是自愿切换：进程主动进入不可运行状态，然后调用 `schedule`。


注意 `schedule()` 与 `context_switch` 的关系：`schedule` 是"决策层"（选 next、改状态），`context_switch` 是"执行层"（保存/恢复寄存器）。这种分层让调度策略可以独立演进——想换算法，只改 `pick_next`；想换架构，只动汇编。真实内核同样如此分层：`schedule()` 内 `__schedule()` 调 `pick_next_task()`，最终落到 `context_switch()`。

另一个细节：`schedule` 内部 `if (next == current) return;` 处理"唯一可运行进程就是自己"的情形——不切换直接返回，避免无谓的上下文切换开销。这个判断在所有调度器里都有对应物，是防抖动的第一道闸。
### （三） 抢占调度 preempt_from_frame

时钟中断触发抢占（见第 6 章 irq.c 的 EOI 后钩子）：

```c
// 时钟中断抢占：把中断现场写入 prev 的上下文后切换。
void preempt_from_frame(struct exception_frame *frame) {
    struct task_struct *next = pick_next();
    if (!next || next == current) return;
    if (next->priority > current->priority) return;   // 不抢占更低优先级

    struct task_struct *prev = current;
    prev->context.x19 = frame->x19;
    ... // 把异常帧的 x19-x29 存进 prev 上下文
    prev->context.x30 = (unsigned long)interrupt_return;
    prev->context.sp  = (unsigned long)frame;

    next->state = PROC_RUNNING;
    current = next;
    switch_to(&next->context);
}
```

**关键点**：被抢占进程的"现场"就是中断时保存的异常帧。`prev->context.sp = frame`（指向异常帧），`prev->context.x30 = interrupt_return`。当 prev 下次被调度时，`switch_to` 恢复寄存器后 `ret` 到 `interrupt_return`，`restore_all` 从异常帧恢复，`eret` 回到被中断的指令——进程完全不知道自己被换出去过。

## 三、定时器：时间从哪里来

### （一） ARM 通用定时器

AArch64 有通用定时器（Generic Timer），基于 `CNTPCT`（物理计数器，单调递增）。v0.3 用虚拟定时器（`CNTV`）：

| 寄存器 | 作用 |
|---|---|
| `cntfrq_el0` | 频率（QEMU virt 下 62.5MHz，日志中 `03b9aca0` = 62500000） |
| `cntv_tval_el0` | 倒计数值：减到 0 触发中断 |
| `cntv_ctl_el0` | 控制：bit 0 ENABLE，bit 1 IMASK |
| `cntpct_el0` | 物理计数（`udelay` 用） |

```c
void timer_init(unsigned int hz) {
    timer_hz = hz;

    asm volatile("mrs %0, cntfrq_el0" : "=r"(timer_freq));

    uart_puts("Timer frequency: ");
    uart_puthex((unsigned int)timer_freq);
    uart_puts(" Hz\n");

    unsigned long interval = timer_freq / hz;
    asm volatile("msr cntv_tval_el0, %0" :: "r"(interval));

    unsigned long ctl = 1;  // ENABLE=1, IMASK=0
    asm volatile("msr cntv_ctl_el0, %0" :: "r"(ctl));

    irq_register(27, timer_interrupt_handler, NULL);

    uart_puts("Timer initialized at ");
    uart_puthex(hz);
    uart_puts(" Hz\n");
}
```

三个要点分别对应硬件资源、换算逻辑和寄存器语义：中断号是 27（PPI 11，第 6 章 GIC 提到）；间隔 = 频率 / 目标频率，100Hz → 每 10ms 一次中断；`ctl = 1` 表示使能且不屏蔽中断。


这三个要点分别对应硬件资源、换算逻辑和寄存器语义。`cntfrq_el0` 在 QEMU virt 下是 62.5MHz（即 0x3b9aca0），意味着计数器每 16ns 跳一次；`interval = 62,500,000 / 100 = 625,000`，即每 625000 个计数跳一次中断，正好 10ms。`ctl` 的 bit 0（ENABLE）使能倒计时，bit 1（IMASK）置 1 则屏蔽中断——v0.3 设为 1 即"使能且不屏蔽"。若将来要一次性定时（one-shot），可以在 handler 里重新设置 `cntv_tval_el0`；v0.3 每次中断都重载间隔，形成周期 tick，这就是"时钟节拍（timer tick）"的由来。
开机日志：

```
Timer frequency: 03b9aca0 Hz
Timer initialized at 00000064 Hz
```

（0x3b9aca0 = 62,500,000；0x64 = 100）


值得记录两个十六进制读数习惯：`03b9aca0` 里 0x3b9aca0 = 3×16^6 + ... = 62,500,000，凑巧是 62.5MHz 的精确值；`00000064` 显然就是 100。QEMU virt 的 `cntfrq_el0` 在不同机器版本上可能不同（有的为 62.5MHz，有的为 24MHz），真实硬件的 CNTFRQ 由 SoC 固件写入——内核**永远不假设频率**，必须在启动时读 `cntfrq_el0` 再换算间隔，这正是 `timer_init` 第一件事就是 `mrs cntfrq_el0` 的原因。若把频率硬编码成 62.5MHz，换一台机器或换 QEMU 机器版本就会导致定时器快慢失准。
### （二） 中断处理

```c
static void timer_interrupt_handler(int irq, void *data) {
    (void)irq; (void)data;
    tick_count++;

    unsigned long interval = timer_freq / timer_hz;
    asm volatile("msr cntv_tval_el0, %0" :: "r"(interval));

    proc_tick();   // 唤醒到期进程 + 请求抢占
}
```

每次中断：tick 计数 +1，重载倒计数值（下一轮），然后调用 `proc_tick()` 驱动调度器。


重载倒计数值放在 handler 开头而不是结尾，是为了减少中断处理窗口的抖动：tick 中断到来时，计数器已归零并触发中断，若不立刻重载，CPU 在 handler 里每多执行一条指令，下一次中断就会晚来相应的时长。QEMU 的虚拟定时器在 `msr cntv_tval_el0` 写入后自动重新开始倒计时，所以 handler 里"先重载、后干活"的顺序最精确。`tick_count` 是全局节拍计数，`get_ticks()` 读取它——第 10 章 `sys_sleep` 的 `wakeup_tick` 正是以它为基准。
## 四、proc_tick：唤醒与抢占请求

```c
// 每个时钟 tick：唤醒到期进程、请求抢占
void proc_tick(void) {
    unsigned long now = get_ticks();
    for (int i = 1; i < MAX_PROCS; i++) {
        struct task_struct *p = &proc_table[i];
        if (p->state == PROC_SLEEPING && p->wakeup_tick <= now)
            p->state = PROC_READY;
    }
    if (pick_next()) need_resched = 1;
}
```

两个职责：

1. **唤醒**：扫描进程表，把所有 `SLEEPING` 且 `wakeup_tick <= now` 的进程置为 READY。`sys_sleep` 设置了 `wakeup_tick = get_ticks() + ticks`，到期自动醒来。
2. **请求抢占**：只要存在就绪进程（且不是当前），置 `need_resched = 1`。IRQ 处理尾声看到这个标志就调用 `preempt_from_frame`（第 6 章）。


这两个职责一个管"时间到没到"，一个管"要不要换人"。唤醒职责隐含了一个设计选择：**所有睡眠进程的唤醒检查都在 tick 里统一做**，而不是每个睡眠者自备定时器。这简化了数据结构（不用维护定时器队列），代价是最坏情况多睡不足 1 tick——对毫秒级 sleep 精度完全可接受。`need_resched` 是一个全局标志，它的意义是"把决策推迟到安全时刻"：tick 中断处理途中不宜切换（内核栈上还压着中断现场），所以 `proc_tick` 只置标志，等 `irq_handler_c` 完成 EOI 之后再看标志决定是否 `preempt_from_frame`。这种"事件 → 置标志 → 安全点消费标志"的模式，是内核里最常见的异步化手法，第 6 章 EOI 顺序、第 13 章信号量唤醒都用同一思路。
**信号量等待者不被 tick 唤醒**：`sema_down` 设置 `wakeup_tick = ~0`（无穷大），`wakeup_tick <= now` 永远不成立，只能由 `sema_up` 显式唤醒（第 13 章）。

## 五、睡眠系统调用

```c
static long sys_sleep(struct exception_frame *frame) {
    unsigned long ms = frame->x0;
    unsigned long ticks = (ms * timer_get_hz() + 999) / 1000;
    if (ticks == 0) ticks = 1;
    current->wakeup_tick = get_ticks() + ticks;
    current->state = PROC_SLEEPING;
    schedule();            // 睡眠期间让出 CPU
    return 0;
}
```

毫秒换算成 tick（向上取整，保证至少睡 1 tick），记录唤醒时刻，转入 SLEEPING，让出 CPU。到期后 `proc_tick` 唤醒，进程从 `schedule()` 返回继续执行。


`sleep` 的精度由换算公式决定：`ticks = (ms * hz + 999) / 1000`。分子加 999 是向上取整——睡 1ms 在 100Hz 下算成 1 tick（10ms），睡 9ms 也是 1 tick。这是"粗粒度时钟"的固有代价：实际睡眠时长是 tick 的整数倍，且向上取整保证"至少睡到用户要求的时间"，绝不少睡。真实内核的 `msleep` 用高精度定时器（hrtimer）挂在红黑树上，精度到纳秒级；教学内核 10ms 粒度足够演示语义，若读者调高 `timer_init` 的 hz（如 1000Hz），睡眠精度会随之提升到 1ms——这是最直观的"用时钟频率换时间精度"实验。

调度器与定时器的耦合点也在此：`proc_tick` 是"定时器中断 → 调度决策"的唯一桥梁，它把硬件节拍翻译成两个调度事件（唤醒睡眠者、请求抢占）。理解这条因果链：硬件计数器归零 → IRQ 27 → `timer_interrupt_handler` → `proc_tick` → `need_resched` → `irq_handler_c` EOI 后 `preempt_from_frame` → `switch_to`。任何一个环节被破坏（比如 IRQ 没注册、`need_resched` 没消费），抢占调度都会静默失效，系统表现为"只有一个进程在跑"——排查这类问题应从这整条链的每一环打印验证，这也是第 17 章综合实验中观察调度的入口。
## 六、僵尸回收：proc_reap

进程退出（`sys_exit`）后变成 ZOMBIE，等父进程 `sys_wait` 回收。但父进程可能比子进程先死——孤儿僵尸谁来收？v0.3 的答案：**idle 循环的 `proc_reap`**。

```c
// 回收僵尸进程（idle 循环中调用）。
// 父进程仍存活且在等待时由 sys_wait 回收，这里跳过。
void proc_reap(void) {
    for (int i = 1; i < MAX_PROCS; i++) {
        struct task_struct *p = &proc_table[i];
        if (p->state != PROC_ZOMBIE) continue;

        int parent_alive = 0;
        if (p->parent_pid > 0) {
            for (int j = 0; j < MAX_PROCS; j++) {
                struct task_struct *q = &proc_table[j];
                if (q != p && q->pid == p->parent_pid && q->state != PROC_UNUSED) {
                    parent_alive = 1;
                    break;
                }
            }
        }
        if (parent_alive) continue;

        uart_puts("[reap] ");
        uart_puts(p->name);
        uart_puts(" pid=");
        uart_puthex((unsigned int)p->pid);
        uart_puts(" exit_code=");
        uart_puthex((unsigned int)p->exit_code);
        uart_puts("\n");
        if (p->stack) free_page((void *)p->stack);
        p->state = PROC_UNUSED;
        p->pid = 0;
    }
}
```

规则：**父进程仍存活 → 交给 `sys_wait` 回收；父进程已死或不存在 → `proc_reap` 回收**（释放内核栈页、清状态）。这避免了僵尸进程无限堆积。


僵尸进程是"已退出但未被回收"的中间态：进程的代码和内存已释放（`sys_exit` 里），但进程表项还留着 `exit_code` 等信息，等待父进程读取。之所以必须保留，是因为 Unix 语义规定"父进程有权获知子进程的退出码"——子进程不能直接把自己从进程表抹掉，否则 `wait` 就无据可查。v0.3 用两条回收路径覆盖所有场景：父进程还活着 → `sys_wait`（或父进程下次 wait）回收；父进程先死（成为孤儿）→ 无人 wait，idle 循环的 `proc_reap` 兜底回收。`proc_reap` 里 `parent_alive` 的判断是 O(n) 查父进程是否存在——对 64 项进程表微不足道。Linux 的对应设计是"孤儿进程收养到 init（pid 1）"：init 对收养的孤儿统一 `wait`，本质相同，只是"兜底者"不同。

真实世界里僵尸堆积是运维故障的常见原因（父进程不 wait 也不退出），本书的 `proc_reap` 让学生从代码层面理解：**僵尸不是 bug，不回收才是**。
`main.c` 的 idle 循环：

```c
// idle 循环：回收僵尸进程，等待中断
for (;;) {
    proc_reap();
    asm volatile("wfi");
}
```

`wfi`（Wait For Interrupt）让 CPU 进入低功耗等待，时钟中断一到就醒来处理，然后继续循环。

## 七、sys_wait：等待子进程

```c
// 等待任一子进程退出，返回其退出码；没有子进程返回 -1
static long sys_wait(struct exception_frame *frame) {
    (void)frame;
    for (;;) {
        int found_child = 0;
        for (int i = 1; i < MAX_PROCS; i++) {
            struct task_struct *p = &proc_table[i];
            if (p->state == PROC_UNUSED || p->parent_pid != current->pid)
                continue;
            found_child = 1;
            if (p->state == PROC_ZOMBIE) {
                int code = p->exit_code;
                if (p->stack) free_page((void *)p->stack);
                p->state = PROC_UNUSED;
                p->pid = 0;
                return code;
            }
        }
        if (!found_child) return -1;

        // 有子进程但还没退出：睡 1 个 tick 再查
        current->wakeup_tick = get_ticks() + 1;
        current->state = PROC_SLEEPING;
        schedule();
    }
}
```

轮询式等待：有子进程但还没退出，睡 1 tick 再查；子进程 ZOMBIE 时回收并返回退出码。shell 的 `run hello` 就是 `fork + execve + wait` 的组合（第 16 章）。


`sys_wait` 采用"睡 1 tick 轮询"而不是"挂在等待队列上被唤醒"，与第 13 章信号量的 `wakeup_tick = ~0` 不同：wait 语义简单（无超时、无信号打断），轮询实现最少 6 行代码。代价是每个 tick 唤醒一次做一次全表扫描（64 项），在 64 进程的负载下开销可忽略。Linux 的 `wait4` 是阻塞式：子进程退出时发 `SIGCHLD`，父进程从 `do_wait` 的内核等待队列醒来——事件驱动，零轮询。两种方案的分水岭是"等待者多不多、唤醒条件多不多"：等待者多就用队列，少就轮询。v0.3 的 `sys_wait` 是理解 Linux `do_wait` 的最佳跳板：它演示了"等待 + 回收 + 退出码传递"的全部语义，只是实现从队列换成了循环。
## 八、ps 命令

`sys_ps` 调 `ps_dump` 打印进程表：

```c
void ps_dump(void) {
    static const char *state_names[] = {
        "UNUSED", "RUNNING", "READY", "SLEEPING", "ZOMBIE"
    };
    uart_puts("PID  NAME        STATE      PRIO\n");
    for (int i = 0; i < MAX_PROCS; i++) {
        struct task_struct *p = &proc_table[i];
        if (p->state == PROC_UNUSED) continue;
        print_dec_pad(p->pid, 5);
        print_fixed(p->name, 12);
        print_fixed(state_names[p->state], 11);
        print_dec_pad(p->priority, 5);
        uart_puts("\n");
    }
}
```

输出示例：

```
PID  NAME        STATE      PRIO
    1  shell        RUNNING      10
```


`ps_dump` 的输出对齐值得解释：`print_dec_pad(pid, 5)` 把十进制数右对齐到 5 个字符宽，`print_fixed(name, 12)` 把名字补齐/截断到 12 字符，`print_fixed(state_names[p->state], 11)` 类似。四列分别按 5/12/11/5 宽度排布，用空格补齐（注意 v0.3 的实现在列间**不加显式分隔空格**，全靠宽度对齐形成列）。调试输出对齐的价值在 ps 里立刻体现：几十个进程一眼扫过，状态与优先级位置固定，比散乱的 tab 分隔好读得多。`state_names` 数组与 `enum proc_state` 的顺序严格一致（UNUSED/RUNNING/READY/SLEEPING/ZOMBIE），靠下标取名字——枚举与字符串数组一一对应是内核日志打印的经典手法，新增状态时必须同步两个定义，读者可把它当作一处"改动时容易漏"的耦合点来体会。

`sys_ps` 由 shell 的 `ps` 命令触发（第 16 章），它直接遍历 `proc_table` 打印所有非 UNUSED 进程。注意 idle（pid 0）也在表中且状态为 RUNNING，所以 ps 输出永远能看到 idle 行；`proc_reap` 回收后的表项 pid 置 0、状态 UNUSED，立即从输出消失。用 ps 验证调度行为的步骤：`run hello` 后立刻 `ps`，应看到 shell（RUNNING）、hello（READY 或 SLEEPING）、idle（RUNNING）三行——如果 hello 在睡眠中，它的 STATE 是 SLEEPING 而不是 READY，这直接验证了 `proc_tick` 的唤醒逻辑。
## 九、main.c 的启动顺序（v0.3 关键调整）

v0.3 把 `timer_init(100)` 移到了**创建 shell 之后**：

```c
// 10. 创建第一个用户进程（shell）
create_test_processes();

// 11. 最后启动时钟中断：此后调度器开始抢占运行 shell
timer_init(100);

uart_puts("Boot complete. Starting scheduler...\n");

// idle 循环：回收僵尸进程，等待中断
for (;;) {
    proc_reap();
    asm volatile("wfi");
}
```

**为什么最后开时钟？** 因为时钟一开，`proc_tick` 就开始抢占调度。如果 shell 还没创建，`pick_next` 可能选到错误的目标；更重要的是一旦时钟开启，CPU 就进入抢占模式，boot 阶段的所有顺序初始化必须在"被打断"之前完成。把 `timer_init` 放最后，保证整个初始化过程不被中断干扰，一气呵成。


这个"最后开时钟"的顺序背后是抢占式内核的一个通用原则：**抢占开启之前，系统必须处于一致状态**。具体到 v0.3：`timer_init(100)` 一旦执行，每 10ms 就有一个 IRQ 打断 CPU，`proc_tick` 会置 `need_resched`，`irq_handler_c` 可能在任意指令边界切换进程。如果 shell 还没创建、`current` 还是 idle，抢占逻辑虽不会出错（`pick_next` 返回 NULL 时 `preempt_from_frame` 直接返回），但没有任何就绪进程可以切换，调度器白白空转；更重要的是，初始化期间 `proc_table` 正在被 `create_test_processes` 填充，若在此刻被抢占，新进程的创建流程会被切走，违背"创建必须原子完成"的直觉。把定时器放最后，让 boot 阶段成为一个不可打断的临界区，是最简单可靠的编排。Linux 启动的 `smp_init` 同样在系统基本就绪后才打开各 CPU 的中断——顺序即正确性。
## 十、小结

本章实现的七块拼图构成完整的调度子系统：静态优先级抢占调度（数值小优先）；主动调度（exit/sleep/信号量）与抢占调度（时钟中断）两条路径；ARM 通用定时器驱动（100Hz tick，PPI 27）；tick 驱动的睡眠唤醒（wakeup_tick 计数）；僵尸回收（proc_reap）+ 孤儿进程处理；sys_wait 轮询等待子进程；抢占请求标志 need_resched。其中"EOI 先于抢占""抢占开启前系统必须一致""睡眠靠计数不靠队列"三个设计决策，是理解整套调度机制的三把钥匙。

运行验证：`run hello` 时 shell 的 `fork` 产生子进程，`ps` 能看到 READY/SLEEPING 状态；hello 打印 3 次 "working" 期间 shell 保持响应（说明抢占生效）。

## 十一、练习

练习一，把两个进程的优先级都设为 1，观察调度行为——同优先级不抢占意味着两个进程轮流获得完整 tick，验证这个推论的依据在 `preempt_from_frame` 的优先级比较处。练习二，修改 `sys_sleep` 的 tick 换算公式（比如去掉向上取整的 `+999`），观察 `run hello` 里三次 "working" 打印的实际间隔变化，从而验证睡眠精度与 tick 粒度的关系。练习三，在 `proc_reap` 里加一个统计计数器（比如静态变量记录回收次数），连续运行多次 `run hello` 后观察回收数量是否与 fork 次数一致，以此验证僵尸回收路径的完整性。
