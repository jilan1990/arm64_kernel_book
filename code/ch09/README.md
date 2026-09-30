# ch09 内核内存分配器（slab） — 本章代码快照

## 本章主题
在 4KB 页分配器之上实现 slab 小对象缓存：把一页切成多个同大小对象、挂空闲链表，按字节粒度分配（`kmalloc/kfree`），解决整页分配造成的内部浪费。

## 新增文件（相对上一章）与新增能力
- 新增文件（对照 frontier 表，共 2 个）：
  - `src/mm/slab.c` —— slab 分配器
  - `include/slab.h` —— `kmalloc/kfree/slab_init` 声明
- 新增能力：
  - 10 个大小级别：`{8,16,32,64,128,256,512,1024,2048,4096}`，2 倍递增，把内部碎片控制在约 25% 均值
  - `struct slab`（一页切成的对象池）+ `struct kmem_cache`（某级别所有 slab 的链表）
  - `slab_create`：从 `alloc_page()` 取一页，描述符之后按 `obj_size` 对齐排布对象，每个对象前 8 字节存空闲链表指针
  - `kmalloc(size)`：≤4096 走对应级别缓存（找有空闲对象的 slab，没有则新建）；>4096 直接分配页
  - `kfree(ptr)`：按页起始地址反推所属 slab，对象挂回空闲链表
  - 分配/释放 O(1)（空闲链表头取/挂），无外部碎片

## 编译与运行
```bash
cd .../code-by-chapter/ch09
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用
```
预期输出（沙箱无 QEMU，未实机验证；依据书稿与源码分析）：
```
Hello, MyOS!
Chapter 4: UART Hello World
```
> 说明：`slab.c` 已链接（`slab.o` 仅引用 ch07 已链接的 `alloc_page` 及 mm.h 宏），但最小版 `kernel_main` **不调用 `slab_init`/`kmalloc`**，启动输出仍只有两行 UART 字串。`main.c` 是全书唯一按书稿演进的文件（ch04–ch17 最小版），本章与真源逐字差异仅此一文件。`kmalloc/kfree` 真正被文件系统（ch15）、网络（ch18）等模块使用。

## 已引入但暂未链接/执行的模块
- `src/boot/exception.S`、`src/kernel/irq.c`（沿用）：仍**不链接**，互锁簇 **ch18** 才链接（原因同前）。
- `src/mm/page_alloc.c`、`src/mm/slab.c` 均**已链接**，但尚未被 `kernel_main` 调用。

## 尚未包含（下一章起才出现）的模块
- 进程控制块 `task_struct`、`proc_init`/`create_kernel_thread`/`create_user_process`/`fork`/`execve`/`schedule`（ch10 `proc.c`）尚未出现；`context_switch` 汇编（ch10）尚未出现。
