# ch10 进程管理与上下文切换 — 本章代码快照

## 本章主题
搭建进程管理基础：进程控制块（PCB）、进程状态机、callee-saved 寄存器上下文切换汇编、内核线程/用户进程创建接口声明。本章链接新增 `context_switch.S`，但核心 `proc.c` 因与异常/中断/定时器/系统调用互锁而**暂不链接**。

## 新增文件（相对上一章）与新增能力
- 新增文件（对照 frontier 表，共 2 个）：
  - `src/proc/proc.c` —— PCB、进程表、状态机、创建/fork/execve/schedule（**本章暂不链接**）
  - `src/proc/context_switch.S` —— 上下文切换汇编（**本章链接**）
- 新增能力：
  - `struct cpu_context`：保存 callee-saved 寄存器 x19–x30 + sp（13 个 unsigned long）
  - `context_switch(old, new)`：把当前 callee-saved 寄存器与 sp 存入 old，恢复 new
  - `switch_to(new)`：只恢复不保存（供抢占路径使用，现场已由 `prev->context` 留存）
  - `struct task_struct`（在 ch08 引入的 `proc.h`）：pid/优先级/状态/内核栈/用户槽位/异常帧伪造/等待队列/僵尸回收等字段
  - 进程状态机：`PROC_UNUSED/RUNNING/READY/SLEEPING/ZOMBIE`，优先级数值越小越高

## 编译与运行
```bash
cd .../code-by-chapter/ch10
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用
```
预期输出（沙箱无 QEMU，未实机验证；依据书稿与源码分析）：
```
Hello, MyOS!
Chapter 4: UART Hello World
```
> 说明：本章链接了 `context_switch.o`（只定义 `context_switch`/`switch_to`，不引用外部符号），但最小版 `kernel_main` 不调用任何进程接口，启动输出仍只有两行 UART 字串。`main.c` 是全书唯一按书稿演进的文件（ch04–ch17 最小版），本章与真源逐字差异仅此一文件。真正的多进程/调度输出要到 ch11（定时器驱动 schedule）并在互锁簇齐全（ch18）后才会出现。

## 已引入但暂未链接/执行的模块
- `src/proc/proc.c` 已拷贝进快照，但**不进入本章 Makefile 链接表**，其未定义引用包括：
  - `interrupt_return`（→ `src/boot/exception.S` 簇）
  - `get_ticks`（→ ch11 `timer.c`）
  - `ramdisk_init` / `uart_char_init`（→ ch14）
  - `shell_prog_*` / `hello_prog_*` / `test_prog_*` / `net_prog_*`（→ ch16 `embed.S`）
  - 与 `irq.c`/`exception.S`/`timer.c`/`syscall.c` 构成互锁簇，**直到 ch18 全栈齐备才一并链接**。
- `src/boot/exception.S`、`src/kernel/irq.c`（沿用）：仍**不链接**（原因同前）。

## 尚未包含（下一章起才出现）的模块
- 定时器驱动与 `proc_tick`/抢占调度（ch11 `timer.c`/`timer.h`）、系统调用入口（ch12）、同步原语（ch13 `spinlock`/`semaphore`）尚未出现；`proc.c` 中声明的 `proc_init`/`schedule`/`fork`/`execve` 要在互锁簇齐全（ch18）后才真正参与链接运行。
