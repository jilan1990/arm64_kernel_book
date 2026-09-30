# ch08 内存布局与 MMU 设计 — 本章代码快照

## 本章主题
在动手实现 MMU 之前先把物理内存布局设计清楚：v0.3 取舍为暂不启用 MMU（恒等映射），靠"用户槽位区 + PC 相对寻址"在无 MMU 下实现 EL1/EL0 特权级隔离，并讲清未来开启 MMU 的路径。本章**只新增头文件，无新 .o**。

## 新增文件（相对上一章）与新增能力
- 新增文件（对照 frontier 表，共 2 个，均为头文件）：
  - `include/proc.h` —— 进程槽位区布局常量与（后续章才实现的）PCB 声明
  - `include/mmu.h` —— `mmu_init()` 前向声明
- 新增能力（设计层面，非新代码编译单元）：
  - 内存布局定型：页分配器管低 128MB（0x40000000~0x48000000）；用户进程槽位区放在其外（0x48000000 起，8×1MB），与内核页分配器互不越界
  - 分清两层概念：**特权级隔离**（EL1/EL0，CPU 硬件保证，v0.3 已实现）vs **地址空间隔离**（MMU 页表，v0.3 暂未实现）
  - 恒等映射模型：虚拟地址 == 物理地址，`mmu_init` 是 page_alloc.c 底部的占位实现，`kernel_main` 连调用都省了
  - 为未来 MMU 预留接口：`mmu_init()` 仅作"未来这里要开 MMU"的提醒

## 编译与运行
```bash
cd .../code-by-chapter/ch08
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用
```
预期输出（沙箱无 QEMU，未实机验证；依据书稿与源码分析）：
```
Hello, MyOS!
Chapter 4: UART Hello World
```
> 说明：本章 Makefile 链接集与 ch07 完全一致（新增的两个文件都是头文件，不产生 .o）。可运行镜像仍由最小版 `main.c` 驱动，输出两行 UART 字串。`main.c` 是全书唯一按书稿演进的文件（ch04–ch17 最小版），本章与真源逐字差异仅此一文件。

## 已引入但暂未链接/执行的模块
- `include/proc.h`、`include/mmu.h` 为头文件，被后续 `proc.c`/`mmu` 代码 include，本身不链接。
- `src/boot/exception.S`、`src/kernel/irq.c`（沿用）：仍**不链接**，互锁簇 **ch18** 才链接（原因同 ch06/ch07）。
- `src/mm/page_alloc.c` 已链接但未被 `kernel_main` 调用。

## 尚未包含（下一章起才出现）的模块
- slab 小对象分配器（ch09 `slab.c`/`slab.h`）尚未出现；`proc.h` 中声明的 PCB/调度/创建接口要到 ch10（`proc.c`）才落地；真正开启 MMU 的多级页表/TCR/MAIR/TTBR 代码在本书 v0.3 主线之外。
