# 第十七章 综合实验：运行用户程序

## 一、概述

经过前面 16 章的开发，我们的内核已经具备了操作系统的所有核心组件：启动代码、异常中断、内存管理、进程调度、系统调用、设备驱动、文件系统和用户空间。这一章我们把所有组件整合起来，完成一个完整的综合实验——从上电启动，到内核初始化，再到运行用户程序，体验完整的操作系统流程。


综合实验的定位不是"新知识"，而是**系统性的验证与串联**：前面每章验证的是单个子系统，这一章验证的是"它们合在一起还能不能工作"。真正的操作系统难题往往出现在组件交界处——比如异常帧在调度切换后还能不能正确恢复（第 6 章与第 10 章的交界）、系统调用返回值在用户态是否可见（第 12 章）、fork 拷贝的用户栈与异常帧平移是否匹配（第 10 章）。这一章通过完整的启动日志、交互命令和真实运行输出，让读者亲手验证这些交界点。

实验的设计原则是"**一条主线、多点验证**"：主线是"启动 → shell → run hello/test → exit 回收"，覆盖启动、调度、系统调用、进程生命周期四大机制；每个命令对应一个可观察的验证点（ps 看调度状态、cat 看文件系统、run 看 fork/exec/wait、exit 看僵尸回收）。读完这一章，读者应该能回答一个问题：**从按下电源到 shell 交互，这 16 章的机制按什么顺序、以什么方式协同工作**。
本章以 v0.3 最终代码为基准，完整走一遍构建与运行。所有日志、命令输出都按 v0.3 实际运行结果整理。

## 二、构建

### （一） 工具链

工具链由三部分组成。交叉编译器 `aarch64-linux-gnu-gcc` 在 x86 宿主机上编译出 ARM64 代码——`-ffreestanding -nostdlib` 告诉它"没有宿主环境、不要链接 libc"，`-mgeneral-regs-only` 禁止使用浮点/向量寄存器（第 4 章讲过原因）。配套工具 `aarch64-linux-gnu-ld`（链接器，配合 `-T` 链接脚本）、`aarch64-linux-gnu-objcopy`（ELF 转裸二进制，把 shell/hello/test 转成内核可直接拷贝的 .bin）、`aarch64-linux-gnu-objdump`（反汇编，调试定位 ELR 地址）。模拟器 `qemu-system-aarch64`（本实验环境为 10.2.1）提供 virt 机器。这些工具的版本不需要精确匹配，但交叉工具链前缀必须统一为 `aarch64-linux-gnu-`——混用前缀是"链接器报架构不兼容"的第一嫌疑。
### （二） make

```bash
cd code
make
```

构建输出（节选，v0.3 真实编译命令）：

```
aarch64-linux-gnu-gcc -Iinclude -g -c src/boot/start.S -o src/boot/start.o
aarch64-linux-gnu-gcc -Iinclude -g -c src/boot/exception.S -o src/boot/exception.o
...
aarch64-linux-gnu-gcc -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -Iinclude -g -O0 -fno-pic -fno-pie -c src/kernel/main.c -o src/kernel/main.o
...
aarch64-linux-gnu-gcc -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -O0 -g -fno-pic -fno-pie -T user/user_link.ld -no-pie -o user/shell.elf user/shell.c
aarch64-linux-gnu-gcc -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -O0 -g -fno-pic -fno-pie -T user/user_link.ld -no-pie -o user/hello.elf user/hello.c
aarch64-linux-gnu-gcc -ffreestanding -nostdlib -nostartfiles -g -T user/user_link.ld -no-pie -o user/test.elf user/test.S
aarch64-linux-gnu-objcopy -O binary user/shell.elf user/shell.bin
aarch64-linux-gnu-objcopy -O binary user/hello.elf user/hello.bin
aarch64-linux-gnu-objcopy -O binary user/test.elf user/test.bin
...
aarch64-linux-gnu-gcc -Iinclude -g -c src/boot/embed.S -o src/boot/embed.o
...
aarch64-linux-gnu-ld -T linker.ld -no-pie -o kernel.elf ...
aarch64-linux-gnu-objcopy -O binary kernel.elf kernel.bin
```

**关于 -O0 的重要说明**：v0.3 的 CFLAGS 明确写死 `-O0 -fno-pic -fno-pie`。`-O0` 不是偷懒，是本书调试中得到的硬结论——早期版本用 `-O2` 时，编译器对异常帧、volatile 访问的优化会让中断路径出现难以排查的寄存器/栈问题，而 `-O0` 让每一步都可预测、可调试（详见第 4 章）。用户程序的 `USER_CFLAGS` 同样用 `-O0 -fno-pic -fno-pie`，保证 PC 相对寻址、可搬移到任意槽位。


`-fno-pic -fno-pie` 的作用值得单独说明：`-fpic`（位置无关代码）会强制所有全局/函数访问走 GOT（全局偏移表），生成额外间接寻址；而 v0.3 的用户程序依赖"直接 PC 相对寻址 + 任意槽位搬移"（第 8 章），`-fno-pic -fno-pie` 关闭 PIE 后，链接器按 `user_link.ld` 的 0x48000000 基址生成直接偏移——搬移时相对关系不变。内核侧同理：`-O0` 保证异常帧布局、寄存器使用完全可预测，配合 GDB 单步能看到"每一条指令都在干什么"。真实内核生产构建用 `-O2`，但教学内核优先可调试性，这是显式的工程取舍而非缺陷。
## 三、运行

```bash
make run
```

实际执行的命令（Makefile 的 run 目标）：

```bash
qemu-system-aarch64 -M virt -m 256M -nographic -cpu cortex-a57 \
        -netdev user,id=net0 \
        -device virtio-net-pci,netdev=net0 \
        -kernel kernel.elf
```

参数含义：

| 参数 | 作用 |
|---|---|
| `-M virt` | QEMU virt 虚拟机（ARM64 标准机器） |
| `-m 256M` | 256MB 内存（页分配器管理 128MB + 用户槽位区 8MB 都放得下） |
| `-nographic` | 无图形界面，UART 直接接到终端 |
| `-cpu cortex-a57` | 指定 CPU 型号 |
| `-netdev user,id=net0` | 用户态网络后端 |
| `-device virtio-net-pci,netdev=net0` | virtio-net 网卡（PCI 形态，第 18 章） |
| `-kernel kernel.elf` | 加载内核镜像 |

## 四、启动日志逐行解读

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

对照 main.c 的启动顺序（第 4、11 章）：

| 步骤 | 初始化 | 日志 |
|---|---|---|
| 1 | `uart_init` | 横幅从这里开始 |
| 2 | `exception_init` | （写 VBAR_EL1，无打印） |
| 3 | `irq_init` | GIC + 开中断（无打印） |
| 4 | `page_alloc_init` | `Page allocator initialized: ...` |
| 5 | `slab_init` | 10 级 slab 缓存（无打印） |
| 6 | `proc_init` | `Process manager initialized` |
| 7 | `syscall_init` | `syscall table initialized` |
| 8 | `device_init` | ram0 + ttyS0（无打印） |
| 9 | `vfs_init` | `VFS (tmpfs) initialized` |
| 10 | `net_init` | `virtio-net: initialized, MAC ...` / `net: protocol stack initialized` |
| 11 | `create_test_processes` | `embedded shell: <大小> bytes` |
| 12 | `timer_init(100)` | `Timer frequency: ...` / `Timer initialized at 00000064 Hz` |
| 13 | idle 循环 | `Boot complete. Starting scheduler...` → `proc_reap()` + `wfi` |

启动顺序有三个值得注意的点。其一，`mmu_init` 没有出现在启动流程里：v0.3 的 `main.c` 只前向声明了它（定义在 `page_alloc.c` 底部，是 stub），**从未调用**——内核实际运行在恒等映射的物理地址空间（第 8 章），这个"接口占位、实际不启用"的设计读者应已从第 8 章理解。其二，`timer_init(100)` 放在最后：时钟一开，抢占调度立刻开始，`create_test_processes` 建好的 shell 就被调度器"启动"——第 11 章解释过这个顺序的意义（boot 初始化必须在不被打断的临界区内完成）。其三，`embedded shell: 00007f20 bytes` 是 shell 二进制的大小，不同构建略有差异（取决于 shell 编译结果），它表明内嵌程序已通过 embed.S 的 `.incbin` 装入内核镜像；shell 的 PID 是 1（`next_pid` 从 1 开始，idle 占 0 号槽）。
## 五、交互实验

### （一） 基本命令

```
$ help
Commands:
  help          - show this help
  echo <text>   - print text
  ps            - list processes
  ls [path]     - list files
  mkdir <path>  - create directory
  cat <file>    - print file content
  run hello     - fork+exec hello program
  run test      - fork+exec test program
  pid           - print shell pid
  clear         - clear screen
  exit          - quit shell
$ echo hello world
hello world
$ cat /etc/motd
Welcome to MyOS! Type 'help' for commands.
$ ls /
etc/
home/
$ ls /etc
motd
$ mkdir /home/abc
created /home/abc
$ ls /home
abc/
```

`ls` 的目录项末尾带 `/`，这是 `vfs_list` 对目录 inode 的标记（第 15 章）。


这组命令覆盖了文件系统的完整读写路径：`help` 走字符串比较分发的命令表；`echo` 走 `sys_write`；`cat /etc/motd` 走"open → read → close"三调用的文件读取全链路；`ls` 走 `vfs_list` 的目录遍历；`mkdir` 走路径拆分与 inode 创建。`cat` 的输出 `Welcome to MyOS! Type 'help' for commands.` 正是 `vfs_init` 预置的 motd 内容——它证明文件系统在启动时正确初始化，且用户态程序能通过系统调用读到内核文件数据。读者可以在 GDB 里对 `tmpfs_read` 下断点，观察一次 `cat` 的逐级调用（第 15 章末尾画过这条链）。
### （二） 多进程：run hello

```
$ run hello
Hello from user space!
my pid = 2
working ... 1/3
working ... 2/3
working ... 3/3
Goodbye!
[child pid=2 exited with 0]
```

hello 每隔 500ms 打印一次 `working ... i/3`（`sys_sleep(500)`）。期间 shell 父进程在 `sys_wait` 里轮询睡眠，**但系统并没有卡死**——时钟中断持续唤醒、驱动多进程轮转。这直接证明了 v0.3 的抢占调度模型（第 11 章）。


`run hello` 是全书最重要的单条命令，因为它一次验证了四个机制：`fork` 创建子进程（第 10 章）、`execve` 换成 hello 映像（第 10 章）、`sys_sleep(500)` 让子进程定时睡眠（第 11 章）、`sys_wait` 父进程轮询回收（第 11 章）。日志里的细节都值得解读：`my pid = 2` 说明子进程拿到了递增的 PID；三次 `working` 间隔 500ms 说明睡眠唤醒按时生效；`[child pid=2 exited with 0]` 是父进程 wait 到 ZOMBIE 后的回收日志。若读者把 hello 里 `sys_sleep(500)` 去掉（改成无限循环），shell 会因 `ps` 无法响应而"看起来卡死"——但这恰好演示了 v0.3 抢占调度的边界：hello 与 shell 同优先级（都是 10），同优先级不抢占，无限循环的 hello 会独占 CPU，只有等到它主动让出（exit/sleep）才能轮到 shell。
### （三） 汇编程序

```
$ run test
Hello, User!
[child pid=3 exited with 0]
```

test 用两条 `svc` 指令（write/exit）完成一次完整的用户态→内核态→用户态旅程（第 16 章）。


`run test` 是最小的用户程序演示：`test.S` 只有十几条指令，`mov x8,#1; mov x0,#1; adr x1,msg; mov x2,#13; svc #0` 发起 write 系统调用，随后 `mov x8,#3; mov x0,#0; svc #0` 发起 exit。它验证的是"不依赖 C 运行时也能做系统调用"——`adr x1,msg` 的 PC 相对寻址让 msg 在任意槽位都指向自己（第 8 章），`svc #0` 是唯一入口（第 12 章）。`[child pid=3 exited with 0]` 说明 wait 回收路径对纯汇编程序同样工作。读者可以用 `objdump -d user/test.elf` 反汇编对照本章的源码，确认每个寄存器的作用。
### （四） 进程表

```
$ ps
PID  NAME        STATE      PRIO
    1shell       RUNNING        10
```

`ps_dump` 用固定宽度打印（pid 5 位、name 12 位、state 11 位、prio 5 位，无额外分隔空格），所以 pid 与名字之间没有空格。在 `run hello` 执行过程中再敲 ps（hello 在睡眠时），能看到：


ps 输出是观察调度器状态的最直接窗口。注意 v0.3 的实现细节：`print_dec_pad(p->pid, 5)` 把 pid 右对齐到 5 个字符宽，`print_fixed(p->name, 12)` 把名字补齐到 12 字符，列间不打印显式空格——所以 `1shell` 连在一起，这是"宽度对齐而非分隔符对齐"的取舍（第 11 章讲过）。hello 睡眠期间它的 STATE 是 SLEEPING，醒来后变 READY，被调度时变 RUNNING——读者反复 `run hello` 并在不同时刻敲 `ps`，能观察到三种状态的完整轮换。
```
$ ps
PID  NAME        STATE      PRIO
    1shell       RUNNING        10
    2hello        SLEEPING      10
```

### （五） 控制台编辑

输入 `echo hello` 后按退格删掉 "hello" 再重打——`console_readline` 的回显和退格处理（第 12 章）让命令行可编辑：

```
$ echo worl
$ echo worl   ← 退格删掉 l 后重打
hello world
```

退格序列 `\b \b`（退格 + 空格 + 退格）把已回显的字符从终端抹掉。


控制台的可编辑性来自第 12 章 `console_readline` 的三类字符处理：可打印字符回显入缓冲、`\r`/`\n` 结束输入、退格（0x7f 或 `\b`）执行擦除。擦除的三字节序列 `\b \b` 是 ANSI 终端标准：退格把光标左移一格、空格覆盖原字符、再退格让光标停在擦除位。读者在终端里亲手试一次 `echo hello` 的退格编辑，就能直观理解"终端只是字节流，编辑逻辑在接收端"这一论断。
## 六、退出

```
$ exit
bye
[exit] shell pid=1 code=0
[reap] shell pid=1 exit_code=0
```

`exit` 命令 → `sys_exit(0)`：内核打印 `[exit] shell pid=1 code=0`，shell 变 ZOMBIE；因为 shell 的父进程是 idle（pid=0，`parent_pid > 0` 判断不成立），idle 循环里的 `proc_reap` 立即回收它（`[reap] shell pid=1 exit_code=0`）。系统回到 idle 循环等待中断（QEMU 中按 `Ctrl-A x` 退出模拟器）。


`exit` 的日志序列完整呈现了僵尸回收的两条路径：`[exit] shell pid=1 code=0` 由 `sys_exit` 打印（第 12 章），记录退出瞬间；`[reap] shell pid=1 exit_code=0` 由 `proc_reap` 打印（第 11 章），记录回收完成。因为 shell 的父进程是 idle（pid=0），`parent_pid > 0` 的判断不成立，`proc_reap` 的"父进程已死"分支命中，idle 循环在下一个 `wfi` 醒来后立即回收。如果 shell 是被某个用户进程 fork 出来的子进程，回收就要等父进程的 `sys_wait`——两种场景读者都可以通过修改 `create_test_processes` 实验验证。
## 七、调试手段

### （一） 同步异常现场

故意触发内核 bug 时（比如访问非法地址），`do_sync_handler` 打印：

```
SYNC EXCEPTION! EC=00000025
  ESR: 96000010
  FAR: 3f000000
  ELR: 400803c4
  SPSR: 00000000
x0 =00000000  x1 =00000000  x2 =00000000  x3 =00000000
x4 =00000000  x5 =00000000  ...
```

EC 是 `(esr >> 26) & 0x3f`；ESR 0x96000010 的 EC = 0x25（Data Abort from current level）。配合 `aarch64-linux-gnu-objdump -d kernel.elf` 反汇编 ELR 地址定位代码（第 6 章）。


同步异常现场的解读是内核调试的第一课。ESR 的低 26 位（0x96000010 的 EC=0x25）告诉我们异常类型是"Data Abort from current level"——访问了不可访问的内存；FAR 是出错地址（0x3f000000，比如访问了未映射的 MMIO 区）；ELR 是被打断指令的地址，配合 `objdump -d kernel.elf` 的地址反查即可定位到具体指令。SPSR 显示异常发生时的处理器状态。三个地址的配合读法：**ELR 告诉"在哪断的"，FAR 告诉"访问了什么"，ESR 告诉"为什么"**。真实内核的 oops 输出同样提供这三个值，读者学会读这套日志，就学会了读 Linux 的 panic 现场。
### （二） 调试打印

v0.3 各初始化阶段都有 `uart_puts` 标记（`Process manager initialized`、`syscall table initialized`、`VFS (tmpfs) initialized`、`virtio-net: initialized, MAC ...`、`embedded shell: ...`）。早期调试版本还有更细的 `PCI: trying ECAM ...`、`virtio: cap@...`、`init: mmu_init` 等打印，v0.3 精简掉了——需要定位问题时，在这些函数里临时加 `uart_puts` 是最直接的手段。


调试打印是内核开发的基本功，但也需要克制：打印太多会淹没关键信息、拖慢时序敏感路径（尤其中断路径）。v0.3 的做法是"保留启动标记、去掉细节探测"——启动日志固定保留 13 行，每个子系统一行；细节（PCI 探测、virtio 能力遍历）只在需要时临时加。给读者一个实践建议：排查问题时按"启动日志定位到子系统 → 在该子系统函数里加细粒度打印 → 修好后移除"的三步走，避免打印永久化。
### （三） 其他目标

Makefile 还提供了：

```bash
make debug      # QEMU 加 -s -S：等待 GDB 连接（target remote :1234）
make run-bin    # 用 -device loader,addr=0x40080000,kernel=kernel.bin 从裸二进制启动
```

`debug` 目标启动后 QEMU 暂停，等 GDB `target remote localhost:1234` 连接；`run-bin` 演示了 ELF 之外的加载方式（链接基址与加载地址必须一致，见第 5 章）。

## 八、常见问题排查

| 现象 | 原因 | 修复 |
|---|---|---|
| `Unimplemented sync exception!` | 异常路径未处理某类异常 | 检查 ESR.EC 分类（第 6 章） |
| `The image is from incompatible architecture` | 编译产物不是 aarch64 | 检查交叉编译器 |
| 链接时 undefined reference | 函数未实现/未加入 Makefile | 对照 main.c 调用链补齐 |
| run hello 无输出 | execve 找不到程序 | 检查 embed.S 内嵌表和名字（第 16 章） |
| 系统卡死 | 时钟未开/中断被屏蔽 | 检查 timer_init 顺序与 DAIF |
| PCI 找不到网卡 | ECAM 基址不对 | 检查 `{0x4010000000, 0x3f000000}`（第 18 章） |
| 用户程序乱码 | 编译成 PIE 或开了优化 | 确认 `-O0 -fno-pic -fno-pie` |


排查表里的每一条都对应一个真实的开发教训。"Unimplemented sync exception"是早期版本最常见的卡点：异常路径只处理了系统调用，其他异常类型（数据中止、未定义指令）没有分支，`do_sync_handler` 打印后停机——修法是按 ESR.EC 补全分类（第 6 章）。"系统卡死"几乎总是时钟问题：`timer_init` 没被调用或中断被屏蔽，抢占调度不启动，系统只剩一个进程在跑——从第 11 章的"最后开时钟"顺序反查。"用户程序乱码"则是编译选项问题：开了 `-O2` 后编译器可能生成依赖未初始化栈的代码，或 PIE 模式下产生绝对地址，搬移到非链接基址的槽位就崩溃——所以 v0.3 死守 `-O0 -fno-pic -fno-pie`。每条现象 → 原因 → 修复的对应关系，本质都是把本书各章的机制知识转成诊断线索。
## 九、小结

综合实验验证了 v0.3 的完整能力链条。启动层：EL2 → EL1 降级、BSS 清零、C 环境建立（第 4、5 章）。内存层：位图页分配器（128MB）与 10 级 slab 缓存协同工作（第 7、9 章）。调度层：优先级抢占调度配合睡眠/唤醒/僵尸回收完整运行（第 11 章）。接口层：16 个系统调用与交互式控制台（回显/退格）支撑了全部用户交互（第 12 章）。文件层：tmpfs 支撑 cat/ls/mkdir 与 motd 欢迎语（第 15 章）。进程层：fork/exec/wait 多进程模型让 run hello / run test 真实运行（第 10 章）。网络层：virtio-net 收发完整打通，`run net` 通过内核 UDP 栈真实解析出 example.com 的 A 记录（第 18 章）。七层环环相扣，从启动到交互再到退出回收，任何一环缺失都会在日志或命令输出中暴露——这正是综合实验的意义：**系统即验证**。
## 十、练习

练习一，给 shell 增加重定向语法 `echo abc > /home/x`：解析 `>` 拆分命令与目标文件，用 `sys_open(path, O_CREAT|O_WRONLY)` 打开（参考第 15 章的 O_CREAT 两阶段语义），把 echo 的输出改写到该 fd——这会让你完整走一遍"shell 解析 → 系统调用 → VFS → tmpfs"的写文件链路。练习二，写一个 `user/loop.c` 死循环（`for(;;){}`），`run loop` 后立刻敲 `ps`——因为 loop 与 shell 同优先级不抢占，你会观察到 shell 无法响应，这直观验证了"同优先级不抢占"的调度边界；把它优先级改成 5 再看效果。练习三，用 `make debug` 启动 QEMU 暂停，GDB `target remote localhost:1234` 连接后在 `timer_interrupt_handler` 下断点，`continue` 后观察每个 tick 的中断处理与 `proc_tick` 的调度钩子（第 11 章）——这是观察抢占路径逐指令执行的最佳方式。
