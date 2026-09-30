# ch16 用户空间与 Shell — 本章代码快照

## 本章主题
把用户空间完整打通：用 `-fno-pic -fno-pie` 的 PC 相对寻址把用户程序编译/链接到 1MB 对齐槽位，经 `embed.S` 的 `.incbin` 内嵌进内核镜像，并准备好 shell / hello / test / net 四个用户程序的构建链。

## 新增文件（相对上一章）与新增能力
本章相对 ch15 新增 5 个文件（其余为前序章节累计拷贝）：

- `user/shell.c`：交互式命令行 shell（11 条内置命令，`run` = fork+exec+wait）。
- `user/hello.c`：用户态打印 + 睡眠演示（`sys_getpid` / `sys_sleep(500)` / `sys_exit`）。
- `user/test.S`：纯汇编系统调用冒烟测试（write + exit，`adr` PC 相对寻址）。
- `user/user_link.ld`：用户程序链接脚本，名义基址 `0x48000000`，导出 `__bss_start/__bss_end`。
- `src/boot/embed.S`：用 `.incbin` 把 `user/{shell,hello,test,net}.bin` 内嵌进内核 `.rodata.embed`，导出 `*_prog_start/*_prog_end` 符号。

说明：`user/user_syscall.h`（16 个系统调用内联封装）与 `user/net.c`（UDP DNS 查询演示）在第 12 章已引入快照集合；本章 `embed.S` 的 `.incbin` 需要全部 4 个 `.bin`，因此 `net.c` 从本章起也参与构建。

新增能力：
- 用户程序独立编译环境（`USER_CFLAGS`/`USER_LDFLAGS`，不链接 libc/crt0，`_start` 自己清 BSS）。
- C/asm → ELF → 裸二进制 → `.incbin` 内嵌进内核镜像的完整构建链。
- `shell/hello/test/net` 四个用户程序均可编译为 `.bin`。

## 编译与运行
```bash
cd .../code-by-chapter/ch16
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用
```
本快照除 `kernel.elf`/`kernel.bin` 外，还会构建 `user/shell.bin`、`user/hello.bin`、`user/test.bin`、`user/net.bin`（由 `src/boot/embed.o: $(USER_BINS)` 依赖强制先建）。

预期输出（沙箱无 QEMU，未实机验证；依据本章 `main.c` 为书稿第四章最小版与源码分析）：
```
Hello, MyOS!
Chapter 4: UART Hello World
```
注意：本章 `kernel_main` 仍是书稿第四章最小版（仅 `uart_init` + 两行打印 + 死循环）。因为 `proc.c` 尚未进入链接（见下节），内嵌的 shell/hello/test/net 二进制虽已 `.incbin` 进镜像，但**没有任何代码去调度运行它们**，内核启动后只打印上述两行便停在最小主循环。完整的 "MyOS v0.3 booting..." 启动日志与 shell 交互要到 ch18（main.c 换最终版、全部子系统链接）才出现。

## 已引入但暂未链接/执行的模块
以下文件已拷贝进快照集合，但**未进入本章 Makefile 的链接表**（保留文件、不编译进 kernel.elf）：

| 文件 | 暂未链接原因 | 生效章节 |
|---|---|---|
| `src/boot/exception.S` | 依赖 irq.o；与中断/调度互锁 | ch18 |
| `src/kernel/irq.c` | 依赖 `handle_syscall`(syscall.c)、`need_resched`/`preempt_from_frame`(proc.c) | ch18 |
| `src/proc/proc.c` | 依赖 `interrupt_return`(exception.S)、`get_ticks`(timer.c)、`ramdisk_init`/`uart_char_init`(ch14 已有)、内嵌程序符号(ch16 已有)；与 irq/exception/timer/syscall 互锁 | ch18 |
| `src/driver/timer.c` | 依赖 `irq_register`(irq.o)、`proc_tick`(proc.o) | ch18 |
| `src/kernel/syscall.c` | 依赖 `vfs_*`(ch15 已有) 与 `udp_*`/`virtio_net_poll`(ch18) | ch18 |
| `src/kernel/semaphore.c` | 依赖 `current`/`schedule`(proc.o) | ch18 |

正因 `proc.c` 未链接，`user_prog_find`/`create_test_processes` 不存在，内嵌用户程序不会被执行。

## 全书唯一例外：main.c
`src/kernel/main.c` 是全书唯一按书稿演进替换的文件：ch04–ch17 使用书稿第四章最小教学版（逐字节等于规格文件 `main.c-ch04-minimal.txt`）；ch18–ch19 才换成 `code/` 最终版（依赖第 18 章 `net_init` 等符号，此前无法链接）。因此本章与 `code/` 的逐文件 diff 差异仅此一文件。

## 尚未包含（下一章起才出现）的模块
- 第 17 章为综合实验，无新增源文件，快照与本章全量一致。
- 第 18 章才加入 PCI/virtio-net 驱动与 ARP/IP/ICMP/UDP 协议栈（`src/kernel/pci.c`、`src/kernel/virtio_net.c`、`src/kernel/net/*`），并把互锁簇（exception/irq/proc/timer/syscall/semaphore）全部链接成完整内核，main.c 换最终版。
