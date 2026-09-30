# 第九章 内核内存分配器

## 一、概述

上一章我们实现了物理页分配器，可以以 4KB 为单位分配和回收内存。但很多时候内核需要分配更小的内存——比如一个 inode 可能只有几百字节，一个文件描述符只有几十字节。如果每次都分配一整页，会造成大量浪费。slab 分配器就是解决这个问题的：**把一页划分成多个同大小的对象，按对象分配**。

为什么要"同大小"而不是像 `malloc` 那样任意大小？因为把页切成固定大小的对象有几个决定性好处：其一，**分配/释放是 O(1)**——对象挂在一个空闲链表上，取头和挂头都是常数操作，不需要像通用分配器那样维护复杂的空闲块结构；其二，**无外部碎片**——页内的对象按固定大小排布，释放的对象立刻可复用，页内永远不会出现"大小不匹配的空洞"；其三，**内存可预测**——一个 slab 页服务一个大小级别，缓存命中率稳定。付出的代价是**内部碎片**：请求 100 字节会拿到 128 字节的对象，浪费 28 字节。10 个 2 倍递增的级别把内部碎片控制在 50% 以内（平均约 25%），这是工程上的经典取舍。

Linux 的 slab 家族（SLAB/SLUB/SLOB）就是这套思想的生产级演化：SLUB 是当前默认，把"每 CPU 缓存、防碎片着色、kmem_cache 统计"都做到了极致，但核心仍是"页切成同大小对象 + 空闲链表"——和本章代码是同一个祖先。先读懂这一章的 200 行，再去看 `mm/slub.c` 的 5000 行就不会迷路。

本章代码对应最终版本 `src/mm/slab.c`、`include/slab.h`。

## 二、slab 设计

v0.3 的 slab 分配器采用"大小级别 + slab 链表 + 空闲链表"结构：

```c
// src/mm/slab.c
#define CACHE_COUNT  10
#define MIN_SIZE     8
#define MAX_SIZE     4096

struct slab {
    struct slab *next;
    int obj_size;
    void *free_list;
    int free_count;
    int total_count;
};

struct kmem_cache {
    size_t obj_size;
    struct slab *slabs;  // 所有 slab 的链表
};

static struct kmem_cache caches[CACHE_COUNT];

// 大小级别表
static const size_t size_table[CACHE_COUNT] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};
```

**10 个大小级别**：8B 到 4096B 按 2 倍递增。请求的大小向上取整到最近的级别（`find_cache_index`）：

```c
// 找到不小于 size 的最小级别
static int find_cache_index(size_t size) {
    for (int i = 0; i < CACHE_COUNT; i++) {
        if (size_table[i] >= size) return i;
    }
    return -1;  // 超过最大大小，需要直接分配页
}
```

级别表为什么是 2 倍递增而不是 1 字节递增？回忆数学：若级别是 `2^k`，任何请求 `size` 的浪费上限是 `2^k - 1` 字节，而分配的对象至少是 `2^{k-1}+1` 字节，浪费比例上限是 `(2^k-1)/(2^{k-1}+1) ≈ 2`——也就是内部碎片最多约 50%，平均约 25%。若级别密集（比如每 8 字节一级），碎片可以压到 7% 以下，但级别数量会爆炸（4096/8 = 512 级），每个级别都要维护 slab 链表。10 级对教学内核是碎片与复杂度的平衡点；真实内核的 kmalloc-16/32/.../8M 也是 2 倍递进，只是级数更多。

`find_cache_index` 是线性扫描，10 级最坏 10 次比较——对内核分配路径可接受；若级别上百，就要换成二分查找或哈希。这也是"教学用线性、工程用对数"的又一例证。

超过 4096 的大对象直接走 `alloc_page`（多页分配），不进 slab：

```c
void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    // 大对象直接分配页
    if (size > MAX_SIZE) {
        int pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        void *ptr = alloc_page();
        for (int i = 1; i < pages && ptr; i++) {
            alloc_page();  // 连续分配（简化：不保证连续）
        }
        return ptr;
    }

    int idx = find_cache_index(size);
    if (idx < 0) return NULL;
    ...
}
```

`size > MAX_SIZE` 的分支展示了"slab 管小、页分配器管大"的职责边界。注意两个细节：`if (size == 0) return NULL` 是防御性检查——内核代码里不少调用方可能传 0，返回 NULL 让上层统一处理空指针；`pages` 的计算 `(size + PAGE_SIZE - 1) / PAGE_SIZE` 是"向上取整页数"的经典公式（`(n + d - 1) / d`）。

> 注意：多页分配的简化处理不保证物理连续（`alloc_page` 是首次适配，可能返回不相邻的页）。这对当前内核足够，但读者应知道真实内核（伙伴分配器）会分配连续页。`kfree` 对多页分配暂不回收，代码中有注释说明。

## 三、slab 的创建

一个 slab 占一整页（4096 字节）。页开头是 slab 描述符（`struct slab`），之后按对象大小划分对象，每个对象的前 8 字节存"下一个空闲对象"的指针，构成空闲链表：

```c
// 创建一个新的 slab（用一页）
static struct slab *slab_create(size_t obj_size) {
    void *page = alloc_page();
    if (!page) return NULL;

    struct slab *slab = (struct slab *)page;
    slab->obj_size = obj_size;
    slab->next = NULL;

    // 对象从 slab 描述符之后开始
    char *obj_start = (char *)page + sizeof(struct slab);
    obj_start = (char *)ALIGN_UP((unsigned long)obj_start, obj_size);

    // 计算能放多少个对象
    char *page_end = (char *)page + PAGE_SIZE;
    slab->total_count = (page_end - obj_start) / obj_size;
    slab->free_count = slab->total_count;

    // 构建空闲链表（每个对象的前 8 字节存下一个对象的指针）
    slab->free_list = NULL;
    for (int i = 0; i < slab->total_count; i++) {
        void *obj = obj_start + i * obj_size;
        *(void **)obj = slab->free_list;
        slab->free_list = obj;
    }

    return slab;
}
```

这段代码里有三个值得逐字品味的细节。

这段代码的三个细节值得逐字品味。`struct slab` 描述符直接放在页开头、页就是 slab，这是"页首描述符"设计：释放时用 `ALIGN_DOWN(ptr, PAGE_SIZE)` 一算就找到描述符，不需要全局注册表，代价是页内少了一小块可用空间（描述符约 32 字节）——真实 SLUB 把描述符放在页外（`struct page` 里），页全部用于对象，因为生产内核的页结构本来就要维护、不额外开销，而教学内核把描述符放页里逻辑最直白。空闲链表指针存在对象内部（`*(void **)obj = slab->free_list`）：空闲对象的前 8 字节被当作 next 指针，对象一分配这个位置就是用户数据、一释放又变回链表指针——这是"对象即节点"（intrusive list）的经典做法，不需要独立链表节点内存，前提是 `obj_size >= 8`，所以级别表从 8 开始（若允许 4 字节对象，指针没地方放）。对象起始地址按对象大小对齐（`ALIGN_UP(obj_start, obj_size)`）：对齐保证任何返回给用户的指针地址都恰好是对象大小整数倍，便于用户做对齐假设；同时 slab 内对象边界清晰，`obj_start + i * obj_size` 永远落在对象起点。若不对齐，`ALIGN_DOWN(ptr, PAGE_SIZE)` 虽仍能定位 slab，但无法从页内偏移推断对象序号，调试会变得困难。

布局示意：

```
┌──────────────────────────┐ 0x...000
│ struct slab 描述符        │
├──────────────────────────┤ obj_start（按 obj_size 对齐）
│ 对象 0  │ next → 对象 1   │
│ 对象 1  │ next → 对象 2   │
│ ...                       │
│ 对象 N-1 │ next → NULL    │
└──────────────────────────┘ 0x...fff
```

分配时从空闲链表头取一个对象（O(1)），释放时把对象重新挂回链表头（O(1)）：

```c
void slab_init(void) {
    for (int i = 0; i < CACHE_COUNT; i++) {
        caches[i].obj_size = size_table[i];
        caches[i].slabs = NULL;
    }
}
```

分配时从空闲链表头取一个对象（O(1)），释放时把对象重新挂回链表头（O(1)）：

```c
void *kmalloc(size_t size) {
    ...
    // 找一个有空闲对象的 slab
    struct slab *slab = cache->slabs;
    while (slab && slab->free_count == 0) {
        slab = slab->next;
    }

    // 没有空闲 slab，创建新的
    if (!slab) {
        slab = slab_create(cache->obj_size);
        if (!slab) return NULL;
        slab->next = cache->slabs;
        cache->slabs = slab;
    }

    // 从空闲链表取一个对象
    void *obj = slab->free_list;
    slab->free_list = *(void **)obj;
    slab->free_count--;

    return obj;
}
```

## 四、kfree：如何找到对象所属的 slab

释放的关键：**对象所在页的起始地址就是 slab 描述符地址**。因为每个 slab 独占一页，`ALIGN_DOWN(ptr, PAGE_SIZE)` 就能定位到 slab：

```c
void kfree(void *ptr) {
    if (!ptr) return;

    // 找到对象所属的 slab（对象所在页的起始地址就是 slab）
    struct slab *slab = (struct slab *)ALIGN_DOWN((unsigned long)ptr, PAGE_SIZE);

    // 大对象（多页）直接释放
    // 注意：这里简化处理，实际需要记录分配的页数
    if (slab->obj_size == 0) {
        // 可能是多页分配，暂时不处理
        return;
    }

    // 归还到空闲链表
    *(void **)ptr = slab->free_list;
    slab->free_list = ptr;
    slab->free_count++;
}
```

释放时不做页级回收（slab 页一旦创建，其对象可反复分配释放）。这是 slab 的高效所在——**对象缓存不销毁，减少页分配/释放的系统开销**。

`kfree` 的 `ALIGN_DOWN(ptr, PAGE_SIZE)` 定位术是整个 slab 最巧妙的单行：对象地址向下对齐到 4KB 就是它所在页的起始，而页起始恰好是 `struct slab`。一次位运算完成"对象 → slab"的定位，不需要哈希表、不需要全局遍历。注意 `kfree` 对 `obj_size == 0` 的检查：多页大对象分配的 `kmalloc` 返回值没有 slab 描述符（页由位图管理），`obj_size` 读到的是相邻内存的随机值，所以真源的 `kfree` 对大对象直接 `return` 不回收——注释明说"简化：暂时不处理"。这是一个"已知限制、明示记录"的教科书示范：宁可明确不支持，也不要假装支持而静默泄漏或越界。

slab 与 Linux 的对应关系也值得梳理：本章的 `caches[10]` ≈ Linux 的 `kmalloc_caches`（每大小一个缓存）；`struct slab` ≈ SLUB 的 `struct slab`（但 SLUB 描述符在 `struct page` 里）；`free_list` ≈ SLUB 的 freelist 指针；`slab_create` ≈ `new_slab`。Linux 还多了三样本章没有的东西：**每 CPU 缓存**（本 CPU 优先取本 CPU 的 slab，避免锁竞争）、**防碎片着色**（把对象起点在页内错开，让不同 slab 的对象不落在同一 cache line 组）、**kmem_cache 导出**（`/proc/slabinfo` 统计）。读懂本章，再看这三样就是"锦上添花"而非"天书"。

## 五、slab 的使用者

v0.3 中 slab 分配器的使用者：

| 使用者 | 分配内容 |
|---|---|
| `tmpfs.c` | inode（`alloc_inode` 用 `kmalloc`） |
| `tmpfs.c` | 文件数据缓冲区（`tmpfs_write` 动态扩容） |
| `net/ip.c` | IP 包与以太网帧缓冲区 |
| `net/udp.c` | UDP 报文缓冲区 |
| `driver/ramdisk.c` | 1MB ramdisk 数据区 |

这些使用者覆盖了 slab 的两类典型负载。`tmpfs` 的 inode 是小而多的对象（几十到几百字节，数量随文件增长），命中 64/128/256 级别；`net` 的报文缓冲区是中等大小的临时对象（几百字节，随收发频次分配释放），命中 512/1024 级别；ramdisk 的 1MB 数据区则走大对象直通页分配器。观察负载分布有个实用技巧：在 `kmalloc` 入口临时打印 `size` 和 `idx`，跑几个操作（mkdir、cat、发包），就能看到每个级别被哪些子系统使用——这也是将来优化 slab 级别设计的第一手数据。

## 六、调试与内存泄漏

slab 分配器是内核内存的"第二条战线"，泄漏往往不表现为"页数下降"而是"某级别对象数只增不减"。调试手段有三个层次。

最直接的是计数观察：给 `struct kmem_cache` 加一个 `alloc_count`/`free_count`，在 `kmalloc`/`kfree` 里增减，通过调试打印定期输出。若某级别 `alloc_count - free_count` 单调增长，说明有对象泄漏（分配后没释放）。`tmpfs` 的文件删除路径是检查重点：`vfs_rmdir` 之后 inode 是否 `kfree` 了。

其次是页级交叉验证：slab 创建时 `free_pages--`，若 slab 永不回收，页数会稳定在某值。结合 `get_free_page_count()` 的输出，能区分"页泄漏"（位图层）与"对象泄漏"（slab 层）。

最后是对越界写（overflow/underflow）的检查：在 `slab_create` 时给每个对象尾部填充一个魔数（如 `0xdeadbeef`），`kfree` 时校验魔数——被破坏说明使用者写了越界。真实内核的 `CONFIG_SLUB_DEBUG` 和 KASAN 做的正是这件事的自动化版本。这一章先掌握手工三层调试法，将来读内核调试代码时能立刻对上号。

## 七、延伸：分配路径的性能观察

slab 的 O(1) 分配在小规模下看不出优势，但读者可以用一个简单实验感受它的"免分配"性质：在 `kmalloc` 与 `alloc_page` 里各加一个计数器，连续创建几十个文件（每次 `alloc_inode` + 数据缓冲），观察 slab 命中率——inode 从已建 slab 里取对象，`alloc_page` 只在首次创建新 slab 时被调用。这个"缓存层吸收高频小分配、页层只承担低频大分配"的分工，正是 Linux 中 slab 存在的根本理由：**系统里 90% 的内核分配是几十到几百字节的小对象，若全部直达页分配器，页表与位图会被高频扰动拖垮**。理解这条分配路径的层次（kmalloc → slab 缓存 → 页位图），再读 Linux 的 `/proc/slabinfo` 输出就会一目了然。

## 八、小结

本章实现的四件事构成内核小对象分配器的完整形态：10 级 slab 缓存（8B~4096B，2 倍递进）、页内空闲链表管理（分配/释放均 O(1)）、大对象直通页分配器（超过 4096 直接 `alloc_page`）、页首 slab 描述符定位（`ALIGN_DOWN`）。与第 7 章的位图页分配器一起，内核有了"页 → 小对象"两级内存服务——大块低频用位图、小块高频用 slab，这正是 Linux buddy + slab 分工的最小投影。

运行验证：`kmalloc` 首次请求某级别时创建 slab 页，`Page allocator initialized ... free pages` 随之减少；`tmpfs` 创建文件和网络发包都会触发分配。也可以临时在 `slab_create` 里打印 `obj_size` 和 `total_count`，直接看到每页对象数的实际值——与第 1 题的推导对照，验证理解。

## 九、练习

练习一，计算对象大小为 64B 时一页能放多少个对象（含 slab 描述符开销）——提示：`sizeof(struct slab)` 约 32 字节，`ALIGN_UP(32, 64) = 64`，`(4096 - 64) / 64 = 63`，这个结果可以与 `slab_create` 里的 `total_count` 打印对照。练习二，实现 slab 页完全空闲时的 `free_page` 回收——提示：给 `struct slab` 加 `in_use` 计数，`kfree` 时递减，归零时从链表摘除并 `free_page`，做完后你会理解"缓存不回收"与"按需回收"的取舍。练习三，跟踪 `tmpfs_write` 的 `kmalloc(new_size)`，分析文件写入时的内存增长模式——观察按需扩容的搬移成本。练习四，把级别表改成 `8, 12, 16, 24, 32, 48, ...` 的非 2 倍序列，对比内部碎片与代码复杂度，思考为什么内核最终选择 2 倍递进——这是一个"碎片率 vs 复杂度"的经典工程权衡。
