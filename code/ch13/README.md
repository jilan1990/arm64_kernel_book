# ch13 同步原语 — 本章代码快照

## 本章主题
用 ARM64 原子指令解决并发竞态：短临界区用忙等自旋锁，长等待用睡眠信号量。

## 新增文件（相对上一章 ch12）与新增能力
- 新增文件：
  - `src/kernel/spinlock.c`
  - `include/spinlock.h`
  - `src/kernel/semaphore.c`
  - `include/semaphore.h`
- 新增能力（从书稿第十三章提取）：
  - 自旋锁 `spinlock`：基于 `LDXR`/`STXR` 独占访问指令对实现 `atomic_xchg`，完成"独占读 → 条件写 → 失败重试"的原子检查并设置，提供 `spin_lock/spin_unlock`，适用于短临界区（纳秒级忙等）。
  - 信号量 `semaphore`：等待者进入 SLEEPING 状态让出 CPU（真实睡眠，不忙转），由 `sema_up` 显式唤醒，提供 `sema_init/sema_down/sema_up`，适用于长等待（毫秒级）。
  - 点明单核教学内核的竞态来源主要是"中断与普通代码交错"，原语本身为多核设计（LDXR/STXR 保证多核互斥）。

## 编译与运行
```bash
cd .../code-by-chapter/ch13
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用
```
预期输出（沙箱无 QEMU，未实机验证；依据书稿与源码分析）：
```
Hello, MyOS!
Chapter 4: UART Hello World
```
说明：本快照 `main.c` 仍为第四章最小教学版，内核运行输出固定为上面两行。本章链接集新增 `spinlock.c`（`spinlock.o` 可独立链接，不依赖后续章符号），但最小 `kernel_main` 并未调用任何加锁路径；信号量 `semaphore.c` 暂不链接（见下），故其睡眠/唤醒逻辑尚未执行。同步原语真正被驱动调用要到全符号齐备（ch18）后。

## 已引入但暂未链接/执行的模块
- `src/kernel/semaphore.c`：已拷贝进快照，但不进入本章 Makefile 链接。原因（`nm` 实证）：`semaphore.o` 引用 `current` 与 `schedule`（属 `proc.o`，ch10 引入；proc.o 与 irq/exception/timer/syscall 互锁，ch18 才可全量链接）。**将在第 18 章生效链接**。
- `src/boot/exception.S`、`src/kernel/irq.c`、`src/proc/proc.c`、`src/driver/timer.c`、`src/kernel/syscall.c`、`user/net.c`：此前各章引入，仍暂不链接/构建（互锁簇 ch18 全链接；user/net.c 自 ch16 构建）。

## 尚未包含（下一章起才出现）的模块
- 第 14 章：设备驱动模型 `char_dev.c/char_dev.h`、`uart_char.c`、`ramdisk.c`、`block_dev.h`（字符/块设备两套框架与 ttyS0、ram0 两个设备）。
- 再往后：tmpfs 文件系统（ch15）、用户态 shell 与内嵌用户程序（ch16）、网络栈（ch18）。
