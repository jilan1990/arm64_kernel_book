# ch07 物理内存管理 — 本章代码快照

## 本章主题
实现基于位图的物理页分配器（buddy 的简化教学版），按 4KB 页粒度跟踪"哪些物理页空闲、哪些被占用"，提供分配/回收接口，为后续 slab（ch09）、进程内核栈（ch10）打基础。

## 新增文件（相对上一章）与新增能力
- 新增文件（对照 frontier 表，共 2 个）：
  - `src/mm/page_alloc.c` —— 位图页分配器
  - `include/mm.h` —— 页大小/内存布局常量与对齐宏
- 新增能力：
  - 内存布局常量：`PAGE_SIZE=4096`、`MEMORY_START=0x40000000`、`MEMORY_SIZE=128MB`（页分配器只管低 128MB）
  - 位图分配器：`page_bitmap[]` 每位代表一页（0 空闲/1 已分配）；`first_free_page` 从内核镜像结束（`_end`）之后开始
  - API：`page_alloc_init()`（标记内核占用页、统计总/空闲页数并打印）、`alloc_page()`（扫描空闲页）、`free_page(addr)`、`get_free_page_count()`
  - 地址/页号互转：`page_to_addr`/`addr_to_page`；提供 `ALIGN_UP/DOWN/PAGE_ALIGN` 宏
  - `mmu_init()` 占位实现（定义在本文件底部）：本构建不启用 MMU，纯接口占位

## 编译与运行
```bash
cd .../code-by-chapter/ch07
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用
```
预期输出（沙箱无 QEMU，未实机验证；依据书稿与源码分析）：
```
Hello, MyOS!
Chapter 4: UART Hello World
```
> 说明：`page_alloc.c` 已链接进 `kernel.elf`，但最小版 `kernel_main` **并不调用 `page_alloc_init`**，因此启动日志仍只有两行 UART 字串，不会打印 "Page allocator initialized: ... total pages, ... free pages"。`main.c` 是全书唯一按书稿演进的文件（ch04–ch17 最小版），本章与真源逐字差异仅此一文件。页分配器被真正调用要等到进程/文件系统/网络模块（ch10 起逐步接入）。

## 已引入但暂未链接/执行的模块
- `src/boot/exception.S`、`src/kernel/irq.c`（沿用 ch06）：仍**不链接**——`irq.o` 缺 `handle_syscall`（ch12）、`need_resched`/`preempt_from_frame`（ch10 `proc.c`）、`vectors`（exception.S）；`exception.o` 缺 `do_sync_handler`/`irq_handler_c`（irq.c）。互锁簇 **ch18** 才链接。
- 本章新增的 `page_alloc.c` 本身**已链接**（仅引用 `_end` 与 uart 符号），但尚未被 `kernel_main` 调用。

## 尚未包含（下一章起才出现）的模块
- `include/proc.h`（进程槽位布局常量）、`include/mmu.h`（ch08，纯头文件）尚未出现；slab 小对象分配器（ch09）、PCB 与上下文切换（ch10）尚未出现。
