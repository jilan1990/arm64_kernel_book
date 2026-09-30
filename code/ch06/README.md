# ch06 异常与中断处理 — 本章代码快照

## 本章主题
建立 AArch64 异常向量表、异常帧保存/恢复机制与 GIC 中断控制器驱动，为系统调用（ch12）、定时器抢占（ch11）等一切"内核入口"打下底座。

## 新增文件（相对上一章）与新增能力
- 新增文件（对照 frontier 表，共 6 个）：
  - `src/boot/exception.S` —— 异常向量表（16 项 ×128B，2KB 对齐）与 `save_all/restore_all` 异常帧汇编
  - `src/kernel/irq.c` —— 同步/IRQ C 处理例程、中断号注册表、抢占挂钩
  - `src/driver/gic.c` —— GIC 分发器/CPU 接口寄存器读写与中断使能
  - `include/exception.h` —— `struct exception_frame`（31 通用寄存器 + sp_el0/elr_el1/spsr_el1）
  - `include/gic.h` —— GIC 寄存器基址与偏移宏
  - `include/irq.h` —— `irq_handler_t` 类型与 IRQ API
- 新增能力：
  - 16 项异常向量表，按"异常源（同步/IRQ/FIQ/SError）× 运行场景（4 种）"编码，`ventry` 宏每项 128B 对齐并填 NOP
  - 统一异常帧：进入异常时 `save_all` 把 34 个寄存器压栈，`irq_handler_c`/`do_sync_handler` 以 `struct exception_frame*` 为参数
  - GIC 驱动：`gic_init`（开分发器/CPU 接口、设优先级掩码）、`gic_enable_irq/disable_irq`、`gic_acknowledge`（读 IAR 取中断号）、`gic_end_of_interrupt`（写 EOI）
  - IRQ 注册中心：`irq_register(irq, handler, data)` 把 handler 挂到 `irq_handlers[256]` 并向 GIC 使能
  - 同步异常按 ESR.EC 分流：SVC（0x15）走 `handle_syscall`，其余打印 ESR/FAR/ELR/SPSR 与全部寄存器现场后停机

## 编译与运行
```bash
cd .../code-by-chapter/ch06
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用
```
预期输出（沙箱无 QEMU，未实机验证；依据书稿与源码分析）：
```
Hello, MyOS!
Chapter 4: UART Hello World
```
> 说明：本章虽新增了异常/GIC 代码，但**可运行镜像仍由最小版 `main.c` 驱动**——`kernel_main` 只初始化 UART 并打印两行字串，并不调用 `gic_init`/`exception_init`。`main.c` 是全书唯一按书稿演进的文件（ch04–ch17 用最小教学版，ch18 才换最终版），因此本章与真源 `code/` 的逐字差异**仅此一文件**。异常/GIC 代码真正被触发要等到 ch11（定时器 IRQ）与 ch12（SVC 系统调用），且需互锁簇齐全（ch18）后才能链接进内核。

## 已引入但暂未链接/执行的模块
- `src/boot/exception.S`、`src/kernel/irq.c` 已拷贝进快照，但**不进入本章 Makefile 链接表**：
  - `irq.o` 未定义引用 `handle_syscall`（→ ch12 `syscall.c`）、`need_resched`/`preempt_from_frame`（→ ch10 `proc.c`）、`vectors`（→ `exception.S`）；
  - `exception.o` 未定义引用 `do_sync_handler`/`irq_handler_c`（均在 `irq.c`）。
  - 二者互相依赖，又各自缺下游符号，构成互锁簇，**直到 ch18 全栈齐备才一并链接**。
  - 已用 `nm` 实证（见交付回报），与 frontier 表一致。

## 尚未包含（下一章起才出现）的模块
- 物理内存管理（ch07 `page_alloc.c`/`mm.h`）、内存布局与 MMU 占位（ch08）、slab 小对象分配器（ch09）、进程控制块与上下文切换（ch10）均未出现。
