# ch12 系统调用 — 本章代码快照

## 本章主题
打通 EL0（用户态）→ EL1（内核态）的受控服务通道：用 `svc` 陷入与统一系统调用号表，让用户程序安全请求内核服务。

## 新增文件（相对上一章 ch11）与新增能力
- 新增文件：
  - `src/kernel/syscall.c`（内核侧系统调用分发与实现）
  - `include/syscall.h`（内核侧系统调用号、异常帧/分发声明）
  - `user/user_syscall.h`（用户态系统调用封装）
  - `user/net.c`（书稿 ch12 引入的 DNS/网络示例用户程序）
- 新增能力（从书稿第十二章提取）：
  - 定义 16 个系统调用号（`SYS_write`=1 … `SYS_net_recv`=16），覆盖输出/输入/进程控制/文件/网络五大类，`MAX_SYSCALL 32` 预留扩展。
  - `handle_syscall` 按调用号分发，并校验参数（调用号范围、fd、count），体现"内核必须校验一切来自用户态的输入"的安全主线。
  - 用户态经 `svc #0` 陷入内核，通用寄存器 x0–x7 传参、x8 传调用号、x0 存返回值；`user_syscall.h` 用内联汇编封装成 `sys_write/read/exit/fork/...`。

## 编译与运行
```bash
cd .../code-by-chapter/ch12
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用
```
预期输出（沙箱无 QEMU，未实机验证；依据书稿与源码分析）：
```
Hello, MyOS!
Chapter 4: UART Hello World
```
说明：本快照 `main.c` 仍为第四章最小教学版，内核运行输出固定为上面两行。`syscall.c` 本章已加入文件集合但暂不链接（见下），故 `handle_syscall` 分发表、各 `sys_*` 尚未参与链接与执行；真正的系统调用用户态行为要到用户程序可构建（ch16）且全符号齐备（ch18）后才可见。

## 已引入但暂未链接/执行的模块
- `src/kernel/syscall.c`：已拷贝进快照，但不进入本章 Makefile 链接。原因（`nm` 实证）：`syscall.o` 引用 `vfs_open/vfs_read/vfs_write/vfs_close/vfs_list/vfs_mkdir`（属第 15 章 tmpfs/VFS）与 `udp_socket/udp_bind/udp_sendto/udp_recvfrom/virtio_net_poll`（属第 18 章网络栈）。**将在第 18 章生效链接**。
- `user/net.c`：书稿 ch12 引入的用户态示例程序；其构建需要 `user_link.ld` 用户链接脚本（第 16 章才引入），**自第 16 章起作为用户程序构建**，本章仅保留源码、不构建。
- `src/boot/exception.S`、`src/kernel/irq.c`、`src/proc/proc.c`、`src/driver/timer.c`：此前各章引入，仍暂不链接（irq/proc/timer/syscall 互锁簇，ch18 全链接）。

## 尚未包含（下一章起才出现）的模块
- 第 13 章：同步原语 `spinlock.c/spinlock.h`、`semaphore.c/semaphore.h`（LDXR/STXR 自旋锁与睡眠信号量）。
- 再往后：字符/块设备驱动模型（ch14）、tmpfs 文件系统（ch15）、用户态 shell 与内嵌用户程序（ch16）、网络栈（ch18）。
