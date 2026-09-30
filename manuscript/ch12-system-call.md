# 第十二章 系统调用

## 一、概述

系统调用（System Call）是用户程序请求内核服务的接口。用户程序运行在低特权级（EL0），不能直接访问硬件或内核数据，需要通过系统调用陷入内核（EL1），由内核代其完成操作。系统调用是用户态和内核态之间的桥梁。


系统调用之所以存在，根源在于**特权级隔离**（第 8 章）：用户程序跑在 EL0，没有权限访问串口、GIC、页表等硬件资源，也不能直接操纵其他进程。内核必须提供一组"受控的服务入口"，用户程序通过它们请求内核代劳。这个设计同时带来两个收益：一是**安全**：内核可以在入口处校验参数（比如 fd 是否越界、缓冲区是否可写），把非法请求挡在门外；二是**抽象**：用户程序不必关心硬件细节，`write(1, buf, n)` 背后是 UART 还是 virtio 控制台，用户无感。

一个常见的疑惑是"系统调用和函数调用有什么区别"。函数调用（BL/RET）在同一个特权级内转移控制流，被调函数与调用者互信；系统调用跨越特权级边界，CPU 硬件负责保存现场、切换栈、校验入口（只能从向量表进入），内核不能信任用户程序传入的任何值。理解"**内核必须校验一切来自用户态的输入**"是这一章的安全主线：v0.3 的 `handle_syscall` 校验调用号范围、`sys_write` 校验 fd、`sys_read` 校验 count，每个校验都对应真实内核 `copy_from_user`/`access_ok` 等机制的最小版本。
v0.3 实现了 **16 个系统调用**，覆盖输出、输入、进程控制、文件操作、网络五大类。本章代码对应最终版本 `src/kernel/syscall.c`、`include/syscall.h`、`user/user_syscall.h`（用户态封装）。

## 二、系统调用号

`include/syscall.h`（内核侧）与 `user/user_syscall.h`（用户侧）使用同一套编号：

```c
// 系统调用号
#define SYS_write   1
#define SYS_read    2
#define SYS_exit    3
#define SYS_fork    4
#define SYS_execve  5
#define SYS_sleep   6
#define SYS_getpid  7
#define SYS_open    8
#define SYS_close   9
#define SYS_ls      10
#define SYS_ps      11
#define SYS_mkdir   12
#define SYS_wait    13
#define SYS_net_socket 14
#define SYS_net_send  15
#define SYS_net_recv  16

#define MAX_SYSCALL 32
```

| 号 | 调用 | 参数 | 返回 |
|---|---|---|---|
| 1 | write | fd, buf, count | 写入字节数 |
| 2 | read | fd, buf, count | 读取字节数 |
| 3 | exit | code | 不返回 |
| 4 | fork | — | 子进程 0 / 父进程 pid |
| 5 | execve | name | 0 / -1 |
| 6 | sleep | ms | 0 |
| 7 | getpid | — | pid |
| 8 | open | path, flags | file 指针 / -1 |
| 9 | close | fd | 0 / -1 |
| 10 | ls | path | 0 / -1 |
| 11 | ps | — | 0 |
| 12 | mkdir | path | 0 / -1 |
| 13 | wait | — | 子进程退出码 / -1 |
| 14 | net_socket | port | sockfd / -1 |
| 15 | net_send | sockfd, data, len, dst_ip, dst_port | 发送字节数 / -1 |
| 16 | net_recv | sockfd, buf, max_len, src_ip, src_port | 字节数 / 0 / -1 |


这张表的 16 个调用覆盖了教学内核需要的全部服务面。编号从 1 开始（0 保留），`MAX_SYSCALL 32` 留足扩展空间。真实 Linux 的系统调用号是**稳定 ABI**：应用编译进 `SYS_write=1`，内核升级也不允许改动编号（改了就破坏二进制兼容），新调用只能往编号表尾部追加。这就是为什么 Linux 的 `__NR_syscalls` 逐年增长而旧号永不变。v0.3 没有 ABI 承诺压力，但同样遵守"编号只增不改"的习惯，读者新增调用时应从 17 开始往后排。
## 三、用户态发起：svc 指令

用户程序通过 `svc #0` 发起系统调用。`user/user_syscall.h` 提供内联封装：

```c
// 内核在 svc 异常中保存/恢复全部通用寄存器，仅 x0 作为返回值被改写
static inline long _syscall3(int nr, long a0, long a1, long a2) {
    register long x8 asm("x8") = nr;
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    asm volatile("svc #0"
                 : "+r"(x0)
                 : "r"(x8), "r"(x1), "r"(x2)
                 : "memory");
    return x0;
}

static inline long _syscall1(int nr, long a0) {
    return _syscall3(nr, a0, 0, 0);
}

static inline long _syscall0(int nr) {
    return _syscall3(nr, 0, 0, 0);
}
```

这段内联汇编是"用户态 → 内核态"的唯一通道，值得逐行读。`register long x8 asm("x8") = nr;` 是 GCC 的寄存器变量语法：把 nr 放进 x8。`asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory")` 中，`volatile` 防止编译器把 svc 优化掉或重排（系统调用有副作用，必须按序执行），`"+r"(x0)` 表示 x0 既是输入（第一个参数）又是输出（返回值），`"memory"` 是内存 clobber，告诉编译器"svc 可能改了内存"，防止它把缓冲区的读写重排到 svc 之后。这三样缺一不可：少了 volatile，优化器可能删除"看似无用"的 svc；少了 memory clobber，缓冲区内容可能在 svc 执行时尚未写回内存。

约定（与 Linux AArch64 一致）是：**x8 放系统调用号，x0-x2 放参数，x0 取返回值**。这套约定由内核与用户共同遵守，是 AArch64 Linux 的正式 syscall ABI，内核 `do_sync_handler` 从 frame->x8 读号、frame->x0 写返回值，用户侧从 x0 读结果，双方不依赖任何内存共享就能完成参数传递。`_syscall1`/`_syscall0` 只是把空参数补零，复用 `_syscall3` 的单一实现，避免写三份汇编。

`svc #0` 触发同步异常（ESR.EC = 0x15），CPU 陷入 EL1，执行向量表中的 `sync_handler`（第 6 章），`do_sync_handler` 识别 EC=0x15 后调用 `handle_syscall`。

## 四、内核侧分发：handle_syscall

```c
void handle_syscall(struct exception_frame *frame) {
    int nr = (int)frame->x8;

    if (nr < 0 || nr >= MAX_SYSCALL || !syscall_table[nr]) {
        frame->x0 = -1;
        return;
    }
    frame->x0 = syscall_table[nr](frame);
}
```

查系统调用表，执行对应处理函数，返回值写回 `frame->x0`（异常返回后用户程序就能读到）。**未注册或越界的调用号返回 -1**。


`handle_syscall` 是内核侧的分发中枢，分发规则：`nr < 0 || nr >= MAX_SYSCALL || !syscall_table[nr]` 三重检查，负数、越界、空表项统一返回 -1。为什么三重？负数来自用户直接传 `-1` 到 x8（无符号问题在 C 的 int 转换后仍需防御）。越界防数组访问。空表项防"编号已保留但未实现"的中间态。真实内核的 `sys_call_table` 同样以"未实现项填 `sys_ni_syscall`（返回 -ENOSYS）"的方式处理，思路一致。

返回值的传递路径值得看清：`frame->x0 = syscall_table[nr](frame)` 把结果写进**异常帧**里的 x0 槽位，`restore_all` 恢复寄存器时 x0 就带着这个值回到用户态，所以用户看到的"返回值"其实是"内核改写后的异常帧 x0"。整条路径没有任何额外寄存器或内存传递，复用第 6 章的异常帧机制，这是"异常帧即系统调用参数载体"的设计红利。
初始化时填充表：

```c
void syscall_init(void) {
    syscall_table[SYS_write]  = sys_write;
    syscall_table[SYS_read]   = sys_read;
    syscall_table[SYS_exit]   = sys_exit;
    syscall_table[SYS_fork]   = sys_fork;
    syscall_table[SYS_execve] = sys_execve;
    syscall_table[SYS_sleep]  = sys_sleep;
    syscall_table[SYS_getpid] = sys_getpid;
    syscall_table[SYS_open]   = sys_open;
    syscall_table[SYS_close]  = sys_close;
    syscall_table[SYS_ls]     = sys_ls;
    syscall_table[SYS_ps]     = sys_ps;
    syscall_table[SYS_mkdir]  = sys_mkdir;
    syscall_table[SYS_wait]   = sys_wait;
    uart_puts("syscall table initialized\n");
}
```

约定：每个处理函数的签名统一为 `long handler(struct exception_frame *frame)`，参数从 frame 的 x0-x2 读取。

## 五、控制台读写

v0.3 新增了交互式控制台：**write 把 `\n` 转成 `\r\n`**（终端需要回车+换行），**read 带回显与退格**。

```c
// 控制台输出：'\n' 转成 '\r\n' 便于终端显示
static void console_write(const char *buf, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (buf[i] == '\n') uart_putc('\r');
        uart_putc(buf[i]);
    }
}

// 控制台读一行：带回显与退格处理
static long console_readline(char *buf, size_t count) {
    size_t i = 0;
    while (i + 1 < count) {
        char c = uart_getc();
        if (c == '\r' || c == '\n') {
            uart_puts("\r\n");
            buf[i++] = '\n';
            break;
        } else if (c == 0x7f || c == '\b') {
            if (i > 0) {
                i--;
                uart_puts("\b \b");
            }
        } else if (c >= 0x20) {
            uart_putc(c);
            buf[i++] = c;
        }
    }
    return (long)i;
}
```
`console_readline` 的行为包括三类字符处理：回车/换行（`\r` 或 `\n`）结束输入，先输出 `\r\n` 把光标移到下一行行首（防止 `\r` 单独出现时光标只回行首不换行），再在缓冲末尾补一个 `\n` 作为行结束标记，返回长度。退格/删除（`0x7f` 或 `\b`）删除已输入字符：输出 `\b \b` 三字节：`\b` 光标左移一位、空格擦除当前字符、再 `\b` 把光标停在擦除后的位置。这是终端擦除一个字符的标准"三连"，任何 ANSI 终端都认识。只有 `c >= 0x20` 的可打印字符才回显并存入缓冲，控制字符（如 `\t`、`\x03`）被静默丢弃，避免用户输入脏数据。

这套行编辑与 Linux 终端的 canonical（行缓冲）模式同源：默认情况下内核 tty 驱动也是"收到回车才算一行、退格可编辑"，只是它还处理了更多转义序列（方向键、HOME/END）。理解 `\b \b` 的擦除原理后，读者就能自己扩展方向键支持（练习 3 的方向）。这使得 shell 有了真正可编辑的命令行。
## 六、write / read

```c
static long sys_write(struct exception_frame *frame) {
    long fd = frame->x0;
    const char *buf = (const char *)frame->x1;
    size_t count = frame->x2;

    if (fd == 1 || fd == 2) {
        console_write(buf, count);
        return (long)count;
    }
    // open() 返回的 file 指针作为文件描述符
    if ((unsigned long)fd > MEMORY_START)
        return vfs_write((struct file *)fd, buf, count);
    return -1;
}

static long sys_read(struct exception_frame *frame) {
    long fd = frame->x0;
    char *buf = (char *)frame->x1;
    size_t count = frame->x2;

    if (count == 0) return 0;

    if (fd == 0)
        return console_readline(buf, count);
    if ((unsigned long)fd > MEMORY_START)
        return vfs_read((struct file *)fd, buf, count);
    return -1;
}
```

v0.3 的 fd 模型很巧妙：**fd 1/2（stdout/stderr）→ 控制台输出；fd 0（stdin）→ 控制台输入。`sys_open` 返回的 `struct file *` 指针直接当作 fd 传给 read/write**。判定用 `(unsigned long)fd > MEMORY_START`。文件指针在内存区，标准 fd 是 0/1/2 小整数，天然区分。


设计要点：这个 fd 模型是 v0.3 最简洁也最需要解释的设计。Unix 的 fd 是**进程独立的整数索引**，指向进程自己的打开文件表；v0.3 砍掉了这层间接，直接让 `sys_open` 返回 `struct file *` 指针本身，`sys_read`/`sys_write` 拿到指针就用。判定"这是文件还是控制台"用 `(unsigned long)fd > MEMORY_START`：0/1/2 是整数，文件指针落在 0x40000000 以上的内存区，两者绝不会撞车。好处是省掉整个 fd 表与文件引用计数，代码量少一个数量级；代价是文件描述符不再有"进程私有"语义，任何进程拿到这个指针都能读写该文件，且没有文件偏移量（offset）管理，每次读写从文件头开始。真实内核的 fd 表还要负责 `O_CLOEXEC`、`dup`、`fcntl` 等一堆语义，教学内核从"指针即 fd"起步，恰好让读者看清 fd 的本质：**一个指向内核文件对象的句柄**。
## 七、进程控制系统调用

### （一） exit：进程退出

```c
static long sys_exit(struct exception_frame *frame) {
    uart_puts("[exit] ");
    uart_puts(current->name);
    uart_puts(" pid=");
    uart_puthex((unsigned int)current->pid);
    uart_puts(" code=");
    uart_puthex((unsigned int)frame->x0);
    uart_puts("\n");

    current->exit_code = (int)frame->x0;
    current->state = PROC_ZOMBIE;
    schedule();            // 切走后不再返回
    return 0;
}
```

流程：记录退出码、转 ZOMBIE、调度让出。**注意 `schedule()` 之后的代码不会再执行**（除非某天被恢复，但 ZOMBIE 不会被调度），所以 `return 0` 只是形式。


`sys_exit` 先打印日志再转 ZOMBIE 的顺序是调试友好的：退出日志在状态翻转之前输出，即使回收路径出问题，也能从日志反推"该进程确实到过 exit"。`exit_code` 记录用户传入的退出码，供父进程 `wait` 读取。`state = PROC_ZOMBIE` 让调度器不再选中它；`schedule()` 交出 CPU。这里有一个教学内核常见的认知误区要澄清：`schedule()` 之后的 `return 0` 永远不会执行，因为 ZOMBIE 状态不可能被 `pick_next` 选中，但它仍然写在代码里，是为了满足"所有路径都有返回值"的编译器约束和阅读者直觉。Linux 的 `do_exit` 同样"不返回"，它调用 `schedule()` 后进入 `ret_from_fork` 死循环兜底，任何返回到调用者都意味着 bug。
### （二） fork/execve/wait/sleep/getpid

直接转发给 proc.c 的实现（第 10、11 章）：
这些转发函数展示了系统调用层与内核逻辑层的分层：`syscall.c` 只做"从异常帧取参数、调实现、传返回值"，真正的语义在 `proc.c`/`timer.c`。`sys_fork` 把整个 frame 传给 `proc_fork`（子进程需要复制父进程的异常帧）；`sys_execve` 从 x0 取程序名；`sys_sleep` 在系统调用层做毫秒到 tick 的换算（第 11 章讲过公式）；`sys_getpid` 直接从 `current` 取。为什么 `sys_sleep` 的换算不放在 `proc.c`？因为换算依赖 `timer_get_hz()`，而睡眠语义在调度器，放在 syscall 层让 proc.c 保持纯调度逻辑。分层边界的选择本身是内核架构设计的日常。

```c
static long sys_fork(struct exception_frame *frame) {
    return proc_fork(frame);
}
static long sys_execve(struct exception_frame *frame) {
    return proc_execve(frame, (const char *)frame->x0);
}
static long sys_getpid(struct exception_frame *frame) {
    (void)frame;
    return current->pid;
}
static long sys_sleep(struct exception_frame *frame) {
    unsigned long ms = frame->x0;
    unsigned long ticks = (ms * timer_get_hz() + 999) / 1000;
    if (ticks == 0) ticks = 1;
    current->wakeup_tick = get_ticks() + ticks;
    current->state = PROC_SLEEPING;
    schedule();
    return 0;
}
```

## 八、文件系统调用

```c
static long sys_open(struct exception_frame *frame) {
    const char *path = (const char *)frame->x0;
    int flags = (int)frame->x1;
    struct file *fp = vfs_open(path, flags);
    if (!fp) return -1;
    return (long)fp;
}

static long sys_close(struct exception_frame *frame) {
    return vfs_close((struct file *)frame->x0);
}

static long sys_ls(struct exception_frame *frame) {
    const char *path = (const char *)frame->x0;
    if (!path) path = "/";
    return vfs_list(path);
}

static long sys_mkdir(struct exception_frame *frame) {
    return vfs_mkdir((const char *)frame->x0);
}
```

对应第 15 章的 VFS 层：`vfs_open`/`vfs_close`/`vfs_list`/`vfs_mkdir`。shell 的 `cat` 命令就是 `sys_open → 循环 sys_read → sys_close`。


文件系统调用的一个关键点：**路径参数是用户空间的字符串指针，v0.3 无 MMU 恒等映射下直接可用**。`vfs_open` 拿到 `(const char *)frame->x0` 就能读字符串，因为它与内核共享同一物理地址空间（第 8 章）。若开了 MMU 且用户空间另有虚拟映射，内核必须先 `copy_from_user` 把字符串拷进内核缓冲再解析。这就是真实内核每个 `getname()` 都要做的安全拷贝的由来。v0.3 的恒等映射省掉了这一步，但读者必须意识到这是"简化"而非"正确做法"：用户程序可以传入任何指针，包括内核数据区，恒等映射下内核会当真去解析。`sys_open` 的返回值设计也呼应上一节的 fd 模型：`return (long)fp` 直接把文件对象指针作为 fd 返回，`sys_close` 又把它还原成指针传给 `vfs_close`。shell 的 `cat` 命令（第 16 章）完整走一遍 `open → read → close` 流程，是这个模型的最小演示。
## 九、sys_wait

```c
// 等待任一子进程退出，返回其退出码；没有子进程返回 -1
static long sys_wait(struct exception_frame *frame) {
    (void)frame;
    for (;;) {
        int found_child = 0;
        for (int i = 1; i < MAX_PROCS; i++) {
            struct task_struct *p = &proc_table[i];
            if (p->state == PROC_UNUSED || p->parent_pid != current->pid)
                continue;
            found_child = 1;
            if (p->state == PROC_ZOMBIE) {
                int code = p->exit_code;
                if (p->stack) free_page((void *)p->stack);
                p->state = PROC_UNUSED;
                p->pid = 0;
                return code;
            }
        }
        if (!found_child) return -1;

        // 有子进程但还没退出：睡 1 个 tick 再查
        current->wakeup_tick = get_ticks() + 1;
        current->state = PROC_SLEEPING;
        schedule();
    }
}
```

## 十、用户态封装

用户程序（shell/hello）通过 `user_syscall.h` 的封装调用：

```c
static inline long sys_write(int fd, const void *buf, size_t count) {
    return _syscall3(SYS_write, fd, (long)buf, (long)count);
}
static inline long sys_read(int fd, void *buf, size_t count) {
    return _syscall3(SYS_read, fd, (long)buf, (long)count);
}
static inline long sys_exit(int code) {
    return _syscall1(SYS_exit, code);
}
static inline long sys_fork(void) {
    return _syscall0(SYS_fork);
}
static inline long sys_execve(const char *name) {
    return _syscall1(SYS_execve, (long)name);
}
...
```

五个及以上的参数走 `_syscall5`（网络调用需要 5 个参数，比 x0-x2 多出 x3、x4）：

```c
static inline long _syscall5(int nr, long a0, long a1, long a2, long a3, long a4) {
    register long x8 asm("x8") = nr;
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    asm volatile("svc #0"
                 : "+r"(x0)
                 : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4)
                 : "memory");
    return x0;
}
```

第 18 章的网络系统调用都经过 `_syscall5`：`sys_net_send` 传 (sockfd, data, len, dst_ip, dst_port)，`sys_net_recv` 传 (sockfd, buf, max_len, src_ip, src_port)：

```c
static inline long sys_net_socket(unsigned int port) {
    return _syscall1(SYS_net_socket, (long)port);
}

static inline long sys_net_send(int sockfd, const void *data, size_t len,
                                unsigned long dst_ip, unsigned int dst_port) {
    return _syscall5(SYS_net_send, (long)sockfd, (long)data, (long)len,
                     (long)dst_ip, (long)dst_port);
}

static inline long sys_net_recv(int sockfd, void *buf, size_t max_len,
                                unsigned long *src_ip, unsigned int *src_port) {
    return _syscall5(SYS_net_recv, (long)sockfd, (long)buf, (long)max_len,
                     (long)src_ip, (long)src_port);
}
```

`user/shell.c` 里没有 libc，所有基础操作（strlen、strcmp、数字转字符串）都是手写的。这正好展示了用户程序在无 libc 环境下如何自给自足。

## 十一、网络系统调用与 UDP DNS 示例

第 18 章实现了内核侧 UDP 协议栈（`include/udp.h` 的 `udp_socket` / `udp_bind` / `udp_sendto` / `udp_recvfrom`）与 virtio-net 收发驱动。本节的三个系统调用把它们开放给用户程序：`net_socket` 建 socket 并绑定本地端口，`net_send` 发 UDP 数据报，`net_recv` 收 UDP 数据报。

### （一） 内核侧实现

`src/kernel/syscall.c` 的末尾，三个网络调用直接转发给第 18 章的 UDP API（与真源逐字一致）：

```c
static long sys_net_socket(struct exception_frame *frame) {
    int port = (int)frame->x0;
    int sockfd = udp_socket();
    if (sockfd < 0) return -1;
    if (udp_bind(sockfd, (uint16_t)port) < 0) return -1;
    return sockfd;
}

// 网络：通过 UDP 发送数据报（dst_ip 为网络字节序数值，如 0x0A000203 = 10.0.2.3）
static long sys_net_send(struct exception_frame *frame) {
    int sockfd = (int)frame->x0;
    const uint8_t *data = (const uint8_t *)frame->x1;
    size_t len = (size_t)frame->x2;
    uint32_t dst_ip = (uint32_t)frame->x3;
    uint16_t dst_port = (uint16_t)frame->x4;
    return udp_sendto(sockfd, data, len, dst_ip, dst_port);
}

// 网络：接收 UDP 数据报（无数据返回 0，返回 -1 表示参数错误）
static long sys_net_recv(struct exception_frame *frame) {
    int sockfd = (int)frame->x0;
    uint8_t *buf = (uint8_t *)frame->x1;
    size_t max_len = (size_t)frame->x2;
    uint32_t *src_ip = (uint32_t *)frame->x3;
    uint16_t *src_port = (uint16_t *)frame->x4;
    virtio_net_poll();   // 轮询收包，投递到 UDP socket 缓冲
    return udp_recvfrom(sockfd, buf, max_len, src_ip, src_port);
}
```

`sys_net_recv` 接收前先调用 `virtio_net_poll()`（第 18 章实现的轮询收包）：内核不是靠中断而是靠用户程序"每次 recv 主动去设备收一轮包"，把到达的 UDP 数据报投递进 socket 缓冲，然后 `udp_recvfrom` 取出数据。这是轮询式网卡在无中断驱动下的必然选择。三个处理器在 `syscall_init` 中注册：

```c
    syscall_table[SYS_net_socket] = sys_net_socket;
    syscall_table[SYS_net_send]   = sys_net_send;
    syscall_table[SYS_net_recv]   = sys_net_recv;
```

### （二） net：用户态 UDP DNS 查询

`user/net.c` 是一个完整的网络用户程序：向 QEMU 用户网络内置的 DNS 服务器（10.0.2.3:53）查询 example.com 的 A 记录，手写 DNS 报文构造与解析（支持压缩指针）。它演示了 `sys_net_socket` / `sys_net_send` / `sys_net_recv` 三个调用在无 libc 环境下的完整用法（与真源 `user/net.c` 逐字一致）：

```c
// user/net.c -- 用户态网络示例：通过内核 UDP 栈发起 DNS 查询
// 向 QEMU 用户网络内置 DNS（10.0.2.3:53）查询 example.com 的 A 记录，
// 演示 sys_net_socket / sys_net_send / sys_net_recv 三个网络系统调用。
#include "user_syscall.h"

extern char __bss_start[], __bss_end[];

static size_t slen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static void print(const char *s) {
    sys_write(1, s, slen(s));
}

static void print_int(long v) {
    char buf[24];
    int i = 23;
    buf[i] = '\0';
    do { buf[--i] = '0' + (char)(v % 10); v /= 10; } while (v);
    print(&buf[i]);
}

// 打印 IPv4 地址（字节序为网络序，先高位后低位）
static void print_ip(const unsigned char *ip) {
    for (int i = 0; i < 4; i++) {
        if (i) print(".");
        print_int(ip[i]);
    }
}

// 构造 DNS 查询报文：查询 <name> 的 A 记录，返回报文长度
static int build_dns_query(const char *name, unsigned char *buf) {
    int off = 0;
    buf[off++] = 0x12; buf[off++] = 0x34;   // 事务 ID
    buf[off++] = 0x01; buf[off++] = 0x00;   // 标志：标准查询（RD=1）
    buf[off++] = 0x00; buf[off++] = 0x01;   // QDCOUNT = 1
    buf[off++] = 0x00; buf[off++] = 0x00;   // ANCOUNT = 0
    buf[off++] = 0x00; buf[off++] = 0x00;   // NSCOUNT = 0
    buf[off++] = 0x00; buf[off++] = 0x00;   // ARCOUNT = 0
    // QNAME：按标签逐段编码，如 "example.com" -> 7example3com0
    while (*name) {
        const char *dot = name;
        while (*dot && *dot != '.') dot++;
        int lablen = (int)(dot - name);
        buf[off++] = (unsigned char)lablen;
        for (int i = 0; i < lablen; i++) buf[off++] = (unsigned char)name[i];
        name = (*dot == '.') ? dot + 1 : dot;
    }
    buf[off++] = 0x00;                       // 根标签结束
    buf[off++] = 0x00; buf[off++] = 0x01;    // QTYPE = A
    buf[off++] = 0x00; buf[off++] = 0x01;    // QCLASS = IN
    return off;
}

// 跳过 DNS 报文中的名称（支持压缩指针）
static int skip_name(const unsigned char *msg, int off, int limit) {
    while (off < limit) {
        unsigned char c = msg[off];
        if (c == 0) return off + 1;                  // 结束标签
        if ((c & 0xC0) == 0xC0) return off + 2;      // 压缩指针（2 字节）
        off += 1 + c;                                 // 普通标签
    }
    return -1;
}

static void run(void) {
    print("[net] UDP DNS query demo (10.0.2.3:53)\n");

    // 1. 创建 UDP socket 并绑定本地端口 40000
    int s = sys_net_socket(40000);
    if (s < 0) {
        print("[net] socket failed\n");
        sys_exit(1);
    }

    // 2. 构造并发送 DNS 查询
    unsigned char query[64];
    int qlen = build_dns_query("example.com", query);
    print("[net] querying example.com -> ");
    long sent = sys_net_send(s, query, (long)qlen, 0x0A000203UL, 53);
    if (sent < 0) {
        print("send failed\n");
        sys_exit(2);
    }
    print_int(sent);
    print(" bytes sent\n");

    // 3. 轮询接收响应（最多约 5 秒，期间内核中断投递 UDP 数据）
    unsigned char resp[512];
    unsigned long src_ip = 0;
    unsigned int src_port = 0;
    long n = 0;
    for (int i = 0; i < 50; i++) {
        n = sys_net_recv(s, resp, sizeof(resp), &src_ip, &src_port);
        if (n > 0) break;
        sys_sleep(100);
    }
    if (n <= 0) {
        print("[net] no response (DNS unreachable?)\n");
        sys_exit(3);
    }

    // 4. 解析响应：跳过头部(12) + 问题段，读第一条答案记录
    int off = 12;
    off = skip_name(resp, off, (int)n);
    if (off < 0) { print("[net] bad response\n"); sys_exit(4); }
    off += 4;                                  // QTYPE + QCLASS
    if (off >= n) { print("[net] bad response\n"); sys_exit(4); }
    off = skip_name(resp, off, (int)n);       // 答案 NAME（可能是指针）
    if (off < 0 || off + 10 >= n) { print("[net] no answer\n"); sys_exit(5); }
    unsigned int rtype = (resp[off] << 8) | resp[off + 1]; off += 2;
    off += 2;                                  // CLASS
    off += 4;                                  // TTL
    unsigned int rdlen = (resp[off] << 8) | resp[off + 1]; off += 2;
    if (rtype == 1 && rdlen == 4) {           // A 记录
        print("[net] example.com = ");
        print_ip(&resp[off]);
        print("\n");
    } else {
        print("[net] no A record\n");
    }

    print("[net] done\n");
    sys_exit(0);
}

__attribute__((section(".text.start")))
void _start(void) {
    for (char *p = __bss_start; p < __bss_end; p++) *p = 0;
    run();
}
```

### （三） 运行效果

在 QEMU 里启动带 `-netdev user` / `-device virtio-net-pci` 的内核，进入 shell 后输入 `run net`：

```text
$ run net
[net] UDP DNS query demo (10.0.2.3:53)
[net] querying example.com -> 29 bytes sent
[net] example.com = 104.20.23.154
[net] done
```

29 字节是 DNS 查询报文长度（12 字节头部 + 7example3com0 + 4 字节 QTYPE/QCLASS = 29）。`example.com = 104.20.23.154` 是 QEMU 用户网络把查询转发给宿主机 DNS 后拿到的真实 A 记录，多次运行可能得到不同地址（如 172.66.147.243），这正是真实 DNS 轮询的表现。从系统调用视角看，`run net` 依次走完：`net_socket`（创建并绑定）→ `net_send`（触发第 18 章的 ARP 解析与 IP 封装）→ 反复 `net_recv`（每次先 `virtio_net_poll` 收包，再取 UDP 数据报）→ 解析打印。**这是全书第一个"用户程序驱动内核网络栈"的完整闭环**。

## 十二、系统调用完整流程

```
shell: sys_write(1, "hello", 5)
   │
   ▼ 内联汇编
mov x8, #1; mov x0, #1; adr x1, msg; mov x2, #5
   │
   ▼ svc #0
CPU 陷入 EL1 → 向量表 sync_handler → save_all
   │
   ▼ do_sync_handler（ESR.EC=0x15）
handle_syscall → syscall_table[1] = sys_write
   │
   ▼ console_write 逐字符输出（\n 转 \r\n）
   │
   ▼ restore_all → eret
返回用户态，x0 = 写入字节数
```


这张流程图的每一段都对应前面的某段代码，建议读者对照反汇编走一遍：`sys_write` 的调用点编译成 `mov x8, #1; mov x0, #1; adr x1, msg; mov x2, #5; svc #0`，注意参数按 x0/x1/x2 排列、调用号在 x8，与 `_syscall3` 的寄存器变量声明对应。`svc #0` 之后 CPU 硬件完成三件事：保存当前状态到 SPSR/ELR、切换到 EL1、跳到 VBAR_EL1 指向的向量表对应入口（更低 EL 用 AArch64 → sync）。`save_all` 把 31 个寄存器压栈形成异常帧，`do_sync_handler` 从 ESR 的 EC 字段（0x15 = SVC）识别出系统调用，`handle_syscall` 查表分发。`console_write` 逐字符输出时 `\n` 转 `\r\n`。这是整个调用唯一真正"干活"的地方。最后 `restore_all` 恢复寄存器（x0 已是返回值），`eret` 把 PC 设回 svc 的下一条指令，用户程序继续执行。**从 svc 到 eret，内核一共在 EL1 停留了不到一百条指令**。这就是系统调用的真实成本，也解释了为什么内核对每次 syscall 的开销敏感（Linux 的 syscall 优化史就是压这几十条指令的历史）。
## 十三、小结

本章完整实现了系统调用链路的每一环。编号与分发表：16 个调用覆盖输出、输入、进程控制、文件操作、网络五大类，`syscall_init` 填充 32 项的 `syscall_table`。用户态发起：`user_syscall.h` 用内联汇编封装 svc，x8 放调用号、x0-x2 放参数、x0 取返回值，与 Linux AArch64 ABI 一致。内核侧分发：`handle_syscall` 三重校验后查表执行，返回值写回异常帧。控制台读写：`\n` 转 `\r\n` 的终端适配、带回显与退格擦除的行编辑。进程控制：exit/fork/execve/sleep/getpid/wait 各自转发到第 10、11 章的实现。文件操作：open/close/ls/mkdir 以"指针即 fd"的简化模型对接第 15 章 VFS。这一章是全书承上启下的枢纽，用户程序与内核的全部交互都汇聚于此。
## 十四、练习

练习一，新增一个 `SYS_gettime` 系统调用，返回 `get_ticks()` 的 tick 计数：需要在内核侧加编号（从 17 起）、在 `syscall_init` 注册、在用户侧加封装，最后在 shell 的某个命令里调用并打印，全程走一遍"编号 → 表项 → 封装 → 调用"的链路，这是理解系统调用最完整的动手路径。练习二，用 `svc #1`（编号 99）之类的未注册调用号调用一次，验证返回值确实是 -1，检查 `handle_syscall` 的越界分支。练习三，修改 `console_readline` 增加光标左右移动，接收 `\033[D`（左移）与 `\033[C`（右移）转义序列后维护一个编辑位置索引，输出时用 `\b` 与重打字符实现。这会让你真正理解终端转义序列的本质：它只是打进终端的数据流，处理逻辑都在接收端（这里就是你的 `console_readline`）。

补记：本章的 16 个系统调用是可扩展的骨架：`MAX_SYSCALL 32` 只用了 16 个，剩下 16 个编号留给读者练习新增调用；而系统调用表 + 分发 + 封装的三层结构，正是阅读真实内核 `arch/arm64/kernel/syscall.c` 与 `sys_call_table` 时的最小认知模型。

下一步，可以挑一个真实需求（比如新增 `SYS_gettime`），按"内核加编号 → 注册处理函数 → 用户加封装"三步走通，体会新增一个系统调用的完整路径。
