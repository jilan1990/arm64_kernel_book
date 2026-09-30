# 第十三章 同步原语

## 一、概述

在多任务系统中，多个进程可能并发访问共享资源，比如全局变量、硬件设备、文件等。如果没有适当的同步，多个进程的执行交错可能导致数据不一致或程序错误，这就是竞态条件（Race Condition）。同步原语就是解决这个问题的工具：自旋锁（短临界区忙等）和信号量（长等待睡眠）。


先澄清一个教学内核的特殊性：v0.3 目前是**单核**（QEMU 默认单 CPU），单核上的竞态来源只有一种——**中断与普通代码的交错**。比如 `sys_write` 正在打印时定时器中断到来，`proc_tick` 修改进程表，若这两段代码共享数据就会冲突。自旋锁与信号量在单核上看起来"永远赢"，但它们是为多核设计的原语：多核时两个 CPU 可以同时执行临界区，只有硬件原子指令（LDXR/STXR）能保证互斥。读者在单核上验证同步原语时，需要人为制造并发（比如练习一的交替打印、或开两个 QEMU CPU 试试），否则竞态永远不会暴露——这也是教学内核"机制先于场景"的典型例子：原语是真实的，触发它们的负载是简单的。

竞态条件的经典示例在本章开头已提到 `alloc_page` 的双重分配。再给一个更贴近日常的例子：shell 的 `ps` 遍历进程表打印时，若另一个进程恰好 `sys_exit` 把表项置 UNUSED，ps 可能读到半更新的状态——在 v0.3 单核+关中断的模型里这不会发生，但这是理解"为什么需要同步"的思维实验：任何共享可变数据的并发访问，都要问一句"如果我在这里被打断会怎样"。
v0.3 的信号量是"真实睡眠"实现：等待者不忙转，而是进入 SLEEPING 状态让出 CPU，由 `sema_up` 显式唤醒。本章代码对应最终版本 `src/kernel/spinlock.c`、`include/spinlock.h`、`src/kernel/semaphore.c`、`include/semaphore.h`。

## 二、竞态条件与临界区

考虑 `alloc_page`：两个进程同时调用，都扫描位图、都发现第 N 页空闲、都把它标记为已分配——结果一个页被分配两次。这就是竞态。临界区是访问共享资源的代码段，必须互斥进入。

解决竞态的两种基本思路：

| 方案 | 适用 | 机制 |
|---|---|---|
| 自旋锁 | 短临界区（纳秒级） | 忙等待，不切换 |
| 信号量 | 长等待（毫秒级或不确定） | 睡眠等待，让出 CPU |


两种方案的根本分歧在于等待者付出什么代价。自旋锁的等待者占着 CPU 反复检查锁（忙等）：临界区只有几条指令时，忙等的开销（几十纳秒）远小于一次进程切换的开销（保存恢复寄存器、改状态、调度决策，微秒级），所以自旋锁是"短临界区的正确选择"；但临界区一旦可能持续毫秒级（比如等待 I/O 完成），忙等就是在浪费整颗 CPU——这时信号量让等待者睡眠让出 CPU，把计算资源让给其他就绪进程。这个权衡可以用一句话记：**切换开销 vs 等待时间，谁大听谁的**。中断上下文中只能自旋不能睡眠（中断处理没有进程上下文可以切换，见第 6 章），所以内核里"中断上下文用 spinlock、进程上下文用 mutex"是铁律。

选择顺序上还有一个工程维度：锁的粒度。大锁（一个全局锁保护所有共享数据）实现简单但并发度低；细锁（每项数据一把锁）并发度高但复杂度和死锁风险上升。v0.3 的 `sem_lock` 是一把全局锁保护所有信号量——单核教学场景足够；Linux 的 rwlock、percpu-rwsem、RCU 等都是为了在"正确性"与"并发度"之间找平衡的演化产物。
## 三、自旋锁

### （一） ARM64 原子指令：LDXR/STXR

自旋锁的核心是"原子地检查并设置"。ARM64 提供独占访问指令对：`LDXR`（独占读）和 `STXR`（独占写）。

```c
// ARM64 原子交换：用 LDXR/STXR 实现
static inline unsigned long atomic_xchg(volatile unsigned long *ptr, unsigned long val) {
    unsigned long old;
    asm volatile(
        "1: ldxr %0, [%1]\n"
        "   stxr %w2, %3, [%1]\n"
        "   cbnz %w2, 1b\n"
        : "=&r"(old)
        : "r"(ptr), "r"(0), "r"(val)
        : "memory"
    );
    return old;
}
```

`LDXR` 读内存并标记独占，`STXR` 写内存——若独占标记仍有效则写入成功（stxr 返回 0），否则失败（返回非 0）重试。这一对指令保证"读-改-写"序列的原子性。


LDXR/STXR 的机制值得深入一层，因为它揭示了硬件如何实现"原子"。`LDXR` 不只是普通读——它同时在一个内部寄存器里记录"我为这个地址建立了独占监视（exclusive monitor）"。之后若其他 CPU 写这块内存，独占监视会被硬件清除。`STXR` 执行时检查监视是否仍有效：有效则写入成功（返回 0）并把监视清除，无效则**不写入**且返回非 0。于是"读旧值 → 写新值"的两步被扩展成"独占读 → 条件写"的循环，任何一步被别的 CPU 打断，条件写都会失败并重试——原子性由硬件的监视状态保证，而非软件锁。

`cbnz %w2, 1b` 是失败重试：stxr 返回值非 0 就跳回 ldxr 重新读。这意味着最坏情况下（高竞争）这个循环可能转很多圈，但每次失败都说明"有别的 CPU 正在操作同一地址"，语义上仍然正确。ARM64 还提供 `CAS`（比较交换）指令族做更高效的单次原子操作，LDXR/STXR 是更底层的通用原语。x86 上对应的实现是 `lock cmpxchg`——x86 在指令级加 `lock` 前缀让总线锁住，ARM64 用独占监视，两者机制不同、语义等价，这是体系结构差异的经典教材案例。
### （二） 加锁与解锁

```c
void spin_lock(spinlock_t *lock) {
    // 尝试原子地把 lock 从 0 改为 1
    while (atomic_xchg(&lock->locked, 1) != 0) {
        // 锁被占用，自旋等待
        // 在循环中可以加 WFE 指令节省功耗
        asm volatile("wfe");
    }
    // 获取锁后，确保之前的内存访问不会重排到临界区内
    asm volatile("dmb ish");
}

void spin_unlock(spinlock_t *lock) {
    // 确保临界区内的内存访问都完成后再释放锁
    asm volatile("dmb ish");
    lock->locked = 0;
    // 唤醒等待的 CPU
    asm volatile("sev");
}
```

要点：

1. `wfe`/`sev`（Wait For Event / Send Event）：自旋时用 `wfe` 进入低功耗等待，`sev` 在解锁时唤醒。比纯忙等省电。
2. `dmb ish`（Data Memory Barrier, inner shareable）：内存屏障。`spin_lock` 末尾的 dmb 保证临界区内的内存访问不会向上重排到锁获取之前；`spin_unlock` 开头的 dmb 保证临界区的访问在解锁前完成。**没有屏障，编译器/CPU 重排会破坏临界区语义**。


这两行注释是自旋锁正确性的全部关键。`wfe`/`sev` 是 ARM 的功耗优化机制：`wfe` 让 CPU 进入低功耗等待，直到收到事件（event）才继续；`sev` 向系统广播一个事件。没有 wfe/sev 的纯忙等（while 循环空转）功能正确但功耗难堪，加了它们后，等待的 CPU 在锁释放前基本不耗电——这是"自旋但不白转"的硬件实现。

`dmb ish` 的作用是**内存序（memory ordering）**。现代 CPU 和编译器都会重排无依赖关系的访存指令（乱序执行、写缓冲、编译优化），若没有屏障，可能出现：临界区里的写操作在锁释放之后才到达内存（读方拿到锁却看不到数据），或临界区读在加锁之前开始（读到旧数据）。`dmb ish` 是"数据内存屏障、inner shareable 域"——`ish` 指定屏障作用域为系统内共享域（所有 CPU 都看到序），这是多核正确性的关键修饰符（换成 `ishst` 只管写序、`sy` 管全系统）。Linux 的 `smp_mb()`/`smp_rmb()`/`smp_wmb()` 在 ARM64 上分别展开为这些指令，读者在 `include/asm-generic/barrier.h` 里能看到它们的完整映射。
```c
int spin_trylock(spinlock_t *lock) {
    // 尝试获取锁，不等待
    return atomic_xchg(&lock->locked, 1) == 0;
}
```

`spin_trylock` 非阻塞：拿不到锁立刻返回 0，调用者可选择其他路径。

## 四、信号量

自旋锁忙等不适合长等待（浪费 CPU）。信号量让等待者**睡眠**：进入 SLEEPING 状态，让出 CPU，由释放者唤醒。

### （一） 数据结构

```c
// include/semaphore.h
typedef struct {
    int count;
    struct task_struct *wait;  // 等待队列
} semaphore_t;
```

### （二） 初始化

```c
void sema_init(semaphore_t *sem, int count) {
    sem->count = count;
    sem->wait = NULL;
}
```

`count > 0` 表示资源可用数量。互斥锁就是 `count = 1` 的信号量。

### （三） sema_down：获取（可能睡眠）

```c
// 信号量等待者用 ~0 表示不按时间唤醒
#define WAIT_FOREVER (~0UL)

void sema_down(semaphore_t *sem) {
    spin_lock(&sem_lock);
    while (sem->count <= 0) {
        // 加入等待队列并睡眠
        current->wait_next = sem->wait;
        sem->wait = current;
        current->wakeup_tick = WAIT_FOREVER;
        current->state = PROC_SLEEPING;
        spin_unlock(&sem_lock);
        schedule();
        spin_lock(&sem_lock);
    }
    sem->count--;
    spin_unlock(&sem_lock);
}
```

流程分解：

1. 加自旋锁保护信号量本身；
2. 若 `count <= 0`：把当前进程挂到 `sem->wait` 队列，设置 `wakeup_tick = ~0`（表示"等信号量"，不被时钟 tick 唤醒），转 SLEEPING，**解锁后 `schedule()` 让出 CPU**；
3. 被 `sema_up` 唤醒后从 `schedule()` 返回，重新加锁，再次检查 `count`（可能已被别的进程抢走，所以是 `while` 循环）；
4. `count > 0` 时 `count--` 获取资源，解锁。

### （四） sema_up：释放并唤醒

```c
void sema_up(semaphore_t *sem) {
    spin_lock(&sem_lock);
    sem->count++;
    // 唤醒一个等待者（其 wakeup_tick = ~0，只能在这里被唤醒）
    if (sem->wait) {
        struct task_struct *p = sem->wait;
        sem->wait = p->wait_next;
        p->wait_next = NULL;
        p->state = PROC_READY;
    }
    spin_unlock(&sem_lock);
}
```

`count++` 释放资源，若等待队列非空则**取出队头进程，置为 READY**。被唤醒的进程在 `proc_tick` 里不会被动（它的 wakeup_tick = ~0 永不过期），只能在这里被唤醒。


信号量的实现里最值得学习的是"**睡眠唤醒的协议分工**"：`sema_down` 负责把进程挂上等待队列并入睡，`sema_up` 负责把队头进程摘下并置为 READY，`proc_tick` 的定时唤醒机制通过 `wakeup_tick = ~0` 明确"不归我管"。这样三个模块各司其职，不会出现"同一个睡眠进程被两个唤醒者同时抢"的竞态。被唤醒的进程从 `schedule()` 返回后**重新加锁、重新检查 count**（while 循环）——因为它在睡眠期间可能有别的进程先抢走了资源，醒来必须重新竞争。这个"唤醒后重查条件"的模式是条件变量/信号量实现的通则，Linux 的 `wait_event` 宏同样在唤醒后检查 `condition` 再决定是否继续等。
> 为什么 wakeup_tick = ~0 是关键设计：时钟 tick 唤醒逻辑（第 11 章）是 `wakeup_tick <= now` 就唤醒。如果信号量等待者也有个具体时间，可能被 tick 误唤醒，在资源未就绪时返回——必须用无穷大标记"我等的不是时间，是信号量"。

### （五） sema_try_down

```c
int sema_try_down(semaphore_t *sem) {
    int ok = 0;
    spin_lock(&sem_lock);
    if (sem->count > 0) {
        sem->count--;
        ok = 1;
    }
    spin_unlock(&sem_lock);
    return ok;
}
```

非阻塞尝试：有资源就取走，没有立刻返回 0。


`try` 变体的存在意义是给调用者"不等待"的选择权：有些路径拿到资源就走，拿不到就改走别的方案（比如放弃操作、稍后重试），而不是被动睡眠。自旋锁有 `spin_trylock`，信号量有 `sema_try_down`，形成对称的 API 家族。真实内核的 `trylock`/`try_down` 用途广泛：网络收包路径拿不到锁就丢包而不是阻塞，这就是典型场景。

历史上信号量的概念由 Dijkstra 于 1965 年提出，P（proberen，测试）与 V（verhoogen，增加）操作对应这里的 `sema_down` 与 `sema_up`。信号量天然支持两类用途：**互斥**（count=1，任意时刻最多一个持有者）与**计数/资源池**（count=N，允许 N 个并发使用者，如限制最多 N 个连接）。v0.3 的 `sema_init(sem, 1)` 就是互斥锁的最小形态；若把 count 设成缓冲容量，就成了生产者-消费者模型的计数信号量。
## 五、信号量 vs 自旋锁：何时用哪个

| 场景 | 用 | 原因 |
|---|---|---|
| 保护短临界区（改一个变量） | 自旋锁 | 睡眠切换的开销 > 忙等 |
| 保护长临界区（I/O 等待） | 信号量 | 睡眠让出 CPU，不浪费 |
| 中断上下文 | 自旋锁 | 中断不能睡眠 |
| 任务协调（生产者/消费者） | 信号量 | 天然支持资源计数 |

Linux 的经典建议：能自旋就不睡，能睡就不自旋——临界区短到纳秒级用自旋锁，可能被阻塞毫秒级用信号量/互斥锁。


场景表还可以再补充两行现代内核的考虑。其一，优先级反转：低优先级进程持有锁，高优先级进程等待锁，中优先级进程抢走 CPU 导致高优进程无限等待——真实内核用"优先级继承"（持有锁时临时提升自己的优先级）解决，这是 RTOS 和 Linux rt-mutex 的标配。其二，**死锁**：两个进程各持一锁、互相等待对方释放——教学内核锁数量少不易触发，但读者写多锁代码时必须遵循"全局一致的加锁顺序"，否则死锁就会在某个并发窗口悄悄出现。v0.3 把这两个问题留给读者思考，正说明同步原语是"实现容易、设计难"的领域。
## 六、当前内核中的使用

v0.3 中信号量的典型场景（作为教学示例，`sema_init`/`sema_down`/`sema_up` 接口已就绪）：

- 保护共享的 slab 缓存（若引入多 CPU 并发分配）；
- 生产者/消费者模型（网络收包缓冲）。

进程表本身由单核串行访问 + 中断关闭保证安全（`schedule`/`proc_tick` 都在内核态执行），所以 spinlock/semaphore 目前主要是机制完整性的体现，为多核扩展铺路。


这个"机制先行、场景后置"的定位是教学内核的常见策略：同步原语作为基础库先实现并验证正确性，等将来引入多核（QEMU 加 `-smp 2`）、中断驱动的并发负载后直接可用。读者可以做一个实验验证单核上竞态确实存在：开两个 QEMU CPU（`-smp 2`），让两个 CPU 同时跑 `ps_dump` 与 `proc_reap`，观察进程表是否出现错乱——若不加锁就出错，正好证明这些原语不是摆设。这也呼应了概述里的论断：**单核隐藏竞态，多核暴露竞态**。
## 七、小结

本章从竞态条件出发实现了完整的同步原语家族。自旋锁部分：用 LDXR/STXR 独占监视指令构造 `atomic_xchg`，配 WFE/SEV 实现低功耗自旋，配 DMB ISH 内存屏障保证临界区内存序，并提供非阻塞的 `spin_trylock`。信号量部分：`sema_down` 在资源不足时把当前进程挂上等待队列、设 `wakeup_tick = ~0` 标记"等信号量而非时间"、解锁后睡眠让出 CPU；`sema_up` 释放资源并显式唤醒队头等待者；唤醒后重查条件的 while 循环保证不会拿错资源；`sema_try_down` 提供非阻塞获取。互斥（count=1）与计数（count=N）两种用法、try 变体家族、以及"能自旋就不睡，能睡就不自旋"的选型原则，共同构成理解 Linux spinlock/mutex/semaphore/RCU 的认知基础。
## 八、练习

练习一，创建两个内核线程（复用第 10 章的 `create_kernel_thread`），各自循环打印自己的名字，用 `sema_init(sem, 1)` 的信号量包裹打印代码，验证交替输出互不重叠——这是信号量互斥语义的直接观察。练习二，把 `sema_down` 里的 `wakeup_tick = WAIT_FOREVER` 注释掉（或改成 `get_ticks() + 1`），让等待者拥有一个具体时刻，观察 `proc_tick` 是否在资源未释放时就把它误唤醒（表现为 `sema_down` 返回但 count 仍为 0 的异常行为）——这能让你亲眼看到 `~0` 标记存在的必要性。练习三，用 `spin_trylock` 实现一个无阻塞的计数器增量函数（拿不到锁就返回"未递增"），与阻塞版对比调用路径的行为差异，理解 try 变体适用的"可放弃"场景。
