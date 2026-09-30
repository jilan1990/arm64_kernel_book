# ch17 综合实验：运行用户程序 — 本章代码快照

## 本章主题
本章不是"新知识"，而是把前 16 章的子系统串起来做系统性验证：从上电启动 → 内核初始化 → shell 交互 → run hello/test → exit 回收。按"一条主线、多点验证"验证组件交界处（异常帧跨调度恢复、系统调用返回值用户态可见、fork 拷贝栈与异常帧平移）。

## 新增文件（相对上一章）与新增能力
- **新增文件：无。** 本章快照 = ch16 全量，文件集合与 ch16 完全相同（42 个源文件）。
- 新增能力：本章在书稿中以 v0.3 最终代码为基准，完整走一遍 `make`/`make run`、逐行解读启动日志、演示 `help/echo/cat/ls/mkdir/ps/run hello/run test/exit` 的交互与僵尸回收。这些是**对既有机制的验证与串联**，不引入新代码。

## 编译与运行
```bash
cd .../code-by-chapter/ch17
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用
```
构建产物同 ch16：`kernel.elf`/`kernel.bin` + `user/{shell,hello,test,net}.bin`。

预期输出（沙箱无 QEMU，未实机验证；依据本章 `main.c` 为最小版与源码分析）：
```
Hello, MyOS!
Chapter 4: UART Hello World
```

### 关于"为什么不是书稿里的完整启动日志"
书稿第十七章以 **v0.3 最终代码**为基准演示，记录了如下完整启动日志（§四）与 shell 会话（§五）：

```
MyOS v0.3 booting...
Page allocator initialized: 00008000 total pages, 00007efb free pages
Process manager initialized
syscall table initialized
VFS (tmpfs) initialized
virtio-net: initialized, MAC 52:54:00:12:34:56
net: protocol stack initialized
embedded shell: 00007f20 bytes
Timer frequency: 03b9aca0 Hz
Timer initialized at 00000064 Hz
Boot complete. Starting scheduler...

MyOS Shell -- type 'help' for commands
$
```

但本快照严格按"逐章演进、文件单调递增"约束：截至本章**尚未加入第 18 章的网络代码**（`pci.c`/`virtio_net.c`/`net/*`），而最终版 `main.c` 的 `net_init()` 依赖第 18 章符号，**在 ch17 无法链接**。因此本快照仍使用书稿最小版 `kernel_main`（只打印两行 Hello World 后死循环），不会执行内嵌 shell。

> 完整的 "MyOS v0.3 booting..." → shell 提示符启动日志、以及 `run hello` / `run test` / `ps` / `exit` 的逐行交互输出，请见 **ch18 / ch19**（或书稿第十七章 §四、§五）——那里 main.c 换成最终版、互锁簇全部链接，才真正跑起完整启动链。

## 已引入但暂未链接/执行的模块
与 ch16 完全相同：`exception.S`、`irq.c`、`proc.c`、`timer.c`、`syscall.c`、`semaphore.c` 已在快照集合中，但因与中断/调度/网络符号互锁，本章仍不链接（详见 ch16 README 表格）。`proc.c` 不链接 → 内嵌 shell/hello/test/net 不被调度执行。

## 全书唯一例外：main.c
同 ch16：本章 `src/kernel/main.c` 仍是书稿第四章最小教学版（逐字节等于规格 `main.c-ch04-minimal.txt`）。与 `code/` 的逐文件 diff 差异仅此一文件。

## 尚未包含（下一章起才出现）的模块
- 第 18 章加入 PCI/virtio-net 驱动与 ARP/IP/ICMP/UDP 协议栈，main.c 换最终版，互锁簇（exception/irq/proc/timer/syscall/semaphore）全部链接成完整内核——届时本章所述的完整启动链日志才真正可运行。
