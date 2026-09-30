# ch11 调度器与定时器 — 本章代码快照

## 本章主题
实现 v0.3 的静态优先级抢占调度策略与 ARM64 通用定时器，为"下一个运行谁"和时间基准奠基。

## 新增文件（相对上一章 ch10）与新增能力
- 新增文件：
  - `src/driver/timer.c`
  - `include/timer.h`
- 新增能力（从书稿第十一章提取）：
  - ARM64 虚拟定时器（`CNTV`）初始化：读 `cntfrq_el0` 频率、设 `cntv_tval_el0` 倒计数值、开 `cntv_ctl_el0`，并通过 `irq_register(27, timer_interrupt_handler, NULL)` 注册时钟中断。
  - 提供 `timer_init(hz)`、`timer_interrupt_handler`、`get_ticks()`、`udelay()` 等时间基准原语。
  - 时钟中断每个 tick 驱动调度：`proc_tick` 推进时间片并触发抢占钩子（书稿中 `pick_next`/`schedule`/`preempt_from_frame` 属 `proc.c`）。

## 编译与运行
```bash
cd .../code-by-chapter/ch11
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用
```
预期输出（沙箱无 QEMU，未实机验证；依据书稿与源码分析）：
```
Hello, MyOS!
Chapter 4: UART Hello World
```
说明：本快照的 `src/kernel/main.c` 使用书稿第四章最小教学版（`uart_init` 后打印两行即死循环），因此内核运行输出固定为上面两行；真正的调度器/定时器日志要到第 18 章网络栈齐备、全符号可链接、`main.c` 换最终版后才会出现。`timer.c` 本章已加入文件集合但暂不链接（见下），故其 `Timer frequency: ...` / `Timer initialized at ...` 打印尚未执行。

## 已引入但暂未链接/执行的模块
- `src/driver/timer.c`：已拷贝进快照，但不进入本章 Makefile 链接。原因：`timer.o` 引用 `irq_register`（属 `irq.o` 簇，见 ch06，而 `irq.o` 依赖 `handle_syscall`→ch12、`need_resched`/`preempt_from_frame`→ch10 的 `proc.o`）与 `proc_tick`（属 `proc.o`，ch10 引入但与 irq/exception/timer/syscall 互锁，ch18 才可全量链接）。**将在第 18 章生效链接**。
- `src/boot/exception.S`、`src/kernel/irq.c`、`src/proc/proc.c`：此前各章引入，同样暂不链接，原因同上（互锁簇，ch18 全链接）。

## 尚未包含（下一章起才出现）的模块
- 第 12 章：系统调用 `syscall.c` / `syscall.h` / `user_syscall.h` / `user/net.c`（EL0→EL1 陷入与 16 个系统调用号）。
- 再往后：同步原语（ch13）、字符/块设备驱动模型（ch14）、tmpfs 文件系统（ch15）、用户态 shell（ch16）、网络栈（ch18）。
