# 第七章 物理内存管理

## 一、概述

内存管理是操作系统最核心的功能之一：内核自身要占内存，进程的内核栈要占内存，用户程序要占内存，网络缓冲区也要占内存，这些物理页由谁分配？物理内存管理器就是回答这个问题的组件：它跟踪哪些物理页空闲、哪些被占用，提供分配和回收接口。

在 Linux 中，物理内存管理被拆成两层：**buddy allocator**（伙伴系统）负责以页为粒度的大块分配与回收，**slab allocator** 负责以字节为粒度的小对象缓存（我们在第 9 章实现 slab）。本书的路径是：第 7 章先做位图页分配器（buddy 的简化版），第 9 章再做 slab；先掌握"页"这一层，再上"字节"这一层。理解了这两个例子，再看 Linux 的 buddy + slab 组合就不会被庞大的代码吓到：核心逻辑其实和这里差不多，只是多了 NUMA、防碎片、per-CPU 页表等工程细节。

本章代码对应最终版本 `src/mm/page_alloc.c`、`include/mm.h`。

## 二、内存布局（v0.3）

v0.3 的内存布局如下：

| 区域 | 地址范围 | 用途 |
|---|---|---|
| RAM 基址 | 0x40000000 | QEMU virt 物理内存起点 |
| 内核镜像 | 0x40080000 起 | 链接脚本定义，含 text/rodata/data/bss/栈 |
| 页分配器管理区 | 0x40000000 ~ 0x48000000（128MB） | `MEMORY_START` 到 `MEMORY_END` |
| 用户进程槽位区 | 0x48000000 ~ 0x48800000（8×1MB） | 用户程序加载区（第 16 章） |
| RAM 顶部 | 0x50000000 | 256MB 结束 |

注意 v0.3 的设计：**页分配器只管理 128MB（0x40000000~0x48000000），用户进程槽位区放在它之外（0x48000000~0x48800000）**。这样用户程序与内核页分配器互不干扰：页分配器永远不会把用户槽位区当作空闲页分配出去，用户程序也不会覆盖内核数据结构。`include/mm.h` 中的定义：

问题：为什么管理区定成 128MB 而不是 QEMU 全部的 256MB？这是 v0.3 的一个明确取舍：`-m 256M` 时 QEMU 提供 256MB RAM，但内核只把低 128MB 纳入页管理，高 128MB（0x48000000 以上）留给用户槽位区和保留区。好处是**布局简单、互不越界**：页分配器、slab、进程栈都从内核镜像之后向上分配，永远碰不到用户区；用户程序从 0x48000000 向上加载，也永远碰不到内核管理的页。代价是浪费了部分内存。对教学内核来说，确定性和安全性优先于内存利用率。如果读者想跑满 256MB，把 `MEMORY_SIZE` 改成 256MB 并重新审视槽位布局即可（练习 1）。

```c
// include/mm.h
#define PAGE_SIZE       4096
#define PAGE_SHIFT      12
#define PAGE_MASK       (~(PAGE_SIZE - 1))

// 内存布局
#define MEMORY_START    0x40000000
#define MEMORY_SIZE     (128 * 1024 * 1024)  // 128MB
#define MEMORY_END      (MEMORY_START + MEMORY_SIZE)

// 内核镜像结束地址（由链接脚本定义）
extern char _end[];

void page_alloc_init(void);
void *alloc_page(void);
void free_page(void *addr);
unsigned long get_free_page_count(void);

// 工具宏
#define ALIGN_UP(x, a)   (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define PAGE_ALIGN(x)    ALIGN_UP(x, PAGE_SIZE)
#endif
```

用户槽位区常量定义在 `include/proc.h`：

```c
#define USER_REGION_BASE 0x48000000UL
#define USER_SLOT_SIZE   0x100000UL
#define USER_SLOTS       8
#define USER_PROG_MAX    0xF0000UL
```

## 三、位图页分配器

v0.3 使用**位图（bitmap）**而不是链表来管理页。每一位代表一个物理页：1 表示已分配，0 表示空闲。

```c
// src/mm/page_alloc.c
#include "mm.h"
#include "uart.h"
#include <stddef.h>

// 物理页总数
#define MAX_PAGES    (MEMORY_SIZE / PAGE_SIZE)

// 位图：每一位代表一个物理页，0=空闲，1=已分配
static unsigned long page_bitmap[MAX_PAGES / (sizeof(unsigned long) * 8)];

// 第一个可分配页的页号（内核镜像之后）
static int first_free_page;

// 总页数和空闲页数
static int total_pages;
static int free_pages;
```

计算：128MB / 4KB = 32768 页。每页占 1 bit，位图只需要 32768/64 = 512 个 `unsigned long` = 4KB，正好一页。

这个数据值得停下来体会：**管理 128MB 内存的元数据只有 4KB**，开销比是 0.003%。位图为什么这么省？因为它的信息密度是"每个物理页一个 bit"——页是物理内存的最小管理单元，页一旦被分配，里面的内容归使用者所有，分配器不再关心其内部结构。相比之下，空闲链表方案要在每个空闲页里存一个 next 指针，既污染空闲页内容（分配前要先擦除指针），又需要额外的头节点管理；位图把元数据集中、固定、与页内容隔离。这是"用空间换简单、用位操作换安全"的典型设计。

另一个细节：`MAX_PAGES` 用 `MEMORY_SIZE / PAGE_SIZE` 在编译期算出（32768），位图数组大小也由编译期常量推导，所以 `page_bitmap` 是一个静态数组而不是动态分配。**分配器本身的元数据必须早于任何分配动作存在**，静态数组保证了这一点。若改成动态分配，就会陷入"分配内存需要先分配内存"的鸡生蛋问题（Linux 用架构相关的低地址静态 `mem_map` 数组解决同样的问题）。

### （一） 页号与地址转换

```c
// 页号转物理地址
static void *page_to_addr(int page) {
    return (void *)(MEMORY_START + page * PAGE_SIZE);
}

// 物理地址转页号
static int addr_to_page(void *addr) {
    return ((unsigned long)addr - MEMORY_START) / PAGE_SIZE;
}
```

页号 i 的物理地址 = MEMORY_START + i × PAGE_SIZE。这是最简单的线性换算。

### （二） 初始化

```c
void page_alloc_init(void) {
    // 计算内核结束后的第一个页号
    unsigned long kernel_end = (unsigned long)_end;
    first_free_page = (kernel_end - MEMORY_START) / PAGE_SIZE;
    if (kernel_end % PAGE_SIZE != 0) {
        first_free_page++;  // 对齐到下一页
    }

    total_pages = MAX_PAGES;

    // 初始时，内核之前和内核占用的页标记为已分配
    for (int i = 0; i < first_free_page; i++) {
        page_bitmap[i / (sizeof(unsigned long) * 8)] |=
            (1UL << (i % (sizeof(unsigned long) * 8)));
    }

    // 其余页标记为空闲
    free_pages = total_pages - first_free_page;

    uart_puts("Page allocator initialized: ");
    uart_puthex(total_pages);
    uart_puts(" total pages, ");
    uart_puthex(free_pages);
    uart_puts(" free pages\n");
}
```

初始化里的位操作是全书最常用的"第 i 位"惯用法，值得拆开讲。给定页号 i，它落在哪个 `unsigned long`、哪一位：

```text
idx = i / 64    // 位图数组下标：每 64 页用一个 unsigned long 记录
bit = i % 64    // 该 word 内的第几位
置位：  page_bitmap[idx] |= (1UL << bit)     // 或运算，把第 bit 位置 1
测试：  page_bitmap[idx] & (1UL << bit)      // 与运算，非零表示该位置 1
清位：  page_bitmap[idx] &= ~(1UL << bit)    // 与非，把第 bit 位清 0
```

例如页号 65：`idx = 1`（第二个 word），`bit = 1`，置位就是 `page_bitmap[1] |= (1UL << 1)`。把"除以 64 得下标、取模 64 得位号"这个模式记熟，位图、页表位、中断掩码等所有位级操作都是同一套路。`_end` 是链接脚本定义的内核镜像结束地址。**内核镜像占用及之前的页全部标记为已分配**，从 `_end` 之后的第一个整页开始才是可分配区。开机日志能看到：

```
Page allocator initialized: 00008000 total pages, 00007efe free pages
```

即 32768 总页，约 32510 空闲页（128MB 中除内核镜像外几乎全部可用）。

### （三） 分配与释放

```c
void *alloc_page(void) {
    // 从 first_free_page 开始扫描空闲页
    for (int i = first_free_page; i < total_pages; i++) {
        int idx = i / (sizeof(unsigned long) * 8);
        int bit = i % (sizeof(unsigned long) * 8);

        if (!(page_bitmap[idx] & (1UL << bit))) {
            // 找到空闲页，标记为已分配
            page_bitmap[idx] |= (1UL << bit);
            free_pages--;
            return page_to_addr(i);
        }
    }

    // 没有空闲页
    uart_puts("alloc_page: out of memory!\n");
    return NULL;
}

void free_page(void *addr) {
    int page = addr_to_page(addr);

    if (page < 0 || page >= total_pages) {
        uart_puts("free_page: invalid address\n");
        return;
    }

    int idx = page / (sizeof(unsigned long) * 8);
    int bit = page % (sizeof(unsigned long) * 8);

    if (!(page_bitmap[idx] & (1UL << bit))) {
        uart_puts("free_page: page already free\n");
        return;
    }

    page_bitmap[idx] &= ~(1UL << bit);
    free_pages++;
}
```

分配策略是**首次适配（first fit）**：从第一个可分配页开始线性扫描，找到第一个空闲页。这简单、确定、无碎片管理开销，对教学内核足够。防御性检查让 `free_page` 对"无效地址"和"重复释放"给出明确报错。

首次适配的行为值得分析：每次分配都从 `first_free_page` 开始扫，所以**低地址区域的页最先被反复占用和释放**，高地址页长期空闲，这会造成"低地址碎片、高地址浪费"的趋势。对我们的负载（几十个页、分配即用即还）毫无影响；但如果是长期运行且频繁分配释放的服务型内核，就该换 buddy 伙伴系统了。Linux 的 buddy 把物理内存按 2 的幂次组织成 11 个阶（order 0~10）的链表，分配时从最小满足的阶取页、必要时分裂大块，释放时尝试合并回更大的块，从机制上抑制碎片。两者对照：位图是"记账型"（记每页状态，用的时候现场找），buddy 是"组织型"（页块按尺寸预组织，取用即得）。第 9 章我们会在位图之上再叠一层 slab 缓存，正好演示这两种思路如何分工：**页层只管大块，字节层用小对象缓存**。

调试小技巧：开机日志的 free pages 数是观察内存行为最方便的指标。正常启动序列里，`Page allocator initialized` 打印后，`slab_init`（第 9 章）会立刻分配若干页、`proc_init`（第 10 章）为每个进程分配内核栈页，所以日志里 free pages 会从约 0x7efd 开始逐步下降；`run hello` 的每次 fork/exec 也各消耗一页，`exit` 后归还。若某次运行日志里 free pages 持续下降且不回涨，多半是内核栈页泄漏（进程退出路径没调 `free_page`）。这是调试内存泄漏的第一现场。

## 四、页分配器的使用者

v0.3 中页分配器的使用者：

| 使用者 | 用途 |
|---|---|
| `slab.c` | 每个 slab 缓存占一页（第 9 章） |
| `proc.c` | 每个进程的内核栈占一页（第 10 章） |
| 内核线程 | 与进程共享同一页栈 |

进程退出时（僵尸回收、`sys_wait`），内核栈页会 `free_page` 还给分配器。整个生命周期闭环：分配 → 使用 → 释放 → 再分配。

三个使用者代表了页分配器的三种典型场景。slab 是**长期持有**型：slab 缓存创建时分配一页，之后长期占据，缓存的生命周期等于系统运行时长（第 9 章）。进程内核栈是**动态生命周期**型：创建进程时分配一页、进程退出时归还，随进程的诞生与死亡高频变化。内核线程则复用进程机制，v0.3 里 idle 进程与内核线程共用同一套 `proc_table` 与栈分配路径。理解每种使用者的生命周期特征，有助于判断一个分配器设计的合理程度：位图首次适配对"分配不多、释放频繁"的场景足够，因为它没有为任何分配模式做专门优化，也就不会在某种模式下退化。

Linux 的物理内存管理演进可以作为参照系：启动早期用 memblock 做简单的连续区域分配（类似我们的"内核镜像之后连续分配"），系统稳定后用 buddy 接管页级分配，buddy 之上再叠 slab/slub 做小对象缓存。本书的顺序（页位图 → slab）正是这条主线的最小投影。先解决"页从哪来"，再解决"小对象从哪来"。

## 五、为什么不用链表

位图 vs 空闲链表：

| 方案 | 优点 | 缺点 |
|---|---|---|
| 位图 | 内存开销固定且极小（4KB/128MB）；分配 O(n) 但 n 小；无需在页内维护指针 | 分配总是线性扫描 |
| 空闲链表 | 分配 O(1)（取链表头） | 每页要存 next 指针，破坏页内容；内存碎片多时需要更复杂的数据结构 |

对教学内核，位图更直观、更安全（空闲页内容不被破坏），v0.3 采用位图。

## 六、延伸阅读：从位图到伙伴系统的演进路径

值得注意：读者学完本章后，最值得做的思维实验是"把首次适配升级成伙伴系统（buddy）"。位图的分配是"现场找空闲页"，buddy 则是"按 2 的幂次预组织空闲块"。内存被划分成 order 0~10 的 11 个块链表，order k 的块大小为 2^k 页。分配时从最小满足的 order 取块，没有则向大 order 借块并逐级分裂。释放时尝试与相邻伙伴合并回大块。两者对比有三个关键指标。分配时间上，位图是 O(n) 扫描，buddy 是 O(1) 取链表头。碎片方面，位图有低地址碎片趋势，buddy 分裂/合并抑制碎片。元数据上，位图只需 4KB 固定 vs buddy 需要 per-page 的 order/伙伴信息）。真实 Linux 的物理页管理就是 buddy + per-cpu 页表缓存，加上 `memblock` 在启动早期的临时分配，形成完整的三层体系。若读者想做这个升级练习，最简起点是：给 `struct page`（一个页的元数据）加 order 字段，实现 `alloc_pages(order)` 与释放合并，代码量约 150 行，做完后对"碎片管理为什么难"会有切身体会。不过，把首次适配升级成 buddy 只是起点，伙伴系统真正的难点在释放时的合并（coalescing）：相邻空闲块如何高效并回高阶链表，需要额外的边界标记或位运算。

一个实战核对技巧：本章的所有结论都可以在 QEMU 里用日志验证。`Page allocator initialized` 后的 free pages 数值、每次 `run hello` 后 fork 消耗一页的递减、exit 后的回涨，构成一张"内存行为心电图"，若某次运行日志的 free pages 曲线与预期不符（比如只减不增），按第 7 章开头的"使用者生命周期表"逐项排查谁分配了没释放，是最高效的泄漏定位法。

## 七、小结

本章实现的四个要点构成页分配器的完整拼图：128MB 物理内存的位图管理（32768 页、512 个 unsigned long 位图字）、内核镜像占用页的初始化标记（`_end` 之前不可分配）、首次适配页分配与释放（含防御性释放检查）、以及与用户槽位区（0x48000000 起 8MB）的隔离设计——页分配器只管 0x40000000~0x48000000 的内核区，用户槽位区由进程管理按槽分配，两者互不侵犯。

运行验证：开机打印 `Page allocator initialized: 00008000 total pages, 00007efd free pages`，随后 slab、进程、网络依次分配页，日志中应看到 free pages 递减。

## 八、练习

练习一：把 `MEMORY_SIZE` 改为 256MB（覆盖 QEMU 的全部 RAM），观察 `MAX_PAGES` 和位图大小的变化——理解内存上限与元数据规模的正比关系。练习二，实现一个 `alloc_pages(n)` 分配连续 n 页的函数（首次适配变体），为将来块设备缓冲区等连续内存需求做准备。练习三，在 `alloc_page` 中加入分配次数计数器，开机后在 `ps` 输出中附带显示——让内存行为可观测，是排查泄漏的第一步。下一步可以给 `free_page` 加双释放检测，或者把计数器做成 `ps` 的常驻字段，让泄漏在发生时就暴露。
