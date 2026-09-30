# 第十六章 用户空间与 Shell

## 一、概述

到目前为止，我们的内核已经有了内存管理、进程调度、系统调用、文件系统等核心功能。但所有代码都运行在内核态（EL1），还没有真正的用户程序运行在用户态（EL0）。这一章我们把用户空间完整打通：编译用户程序、链接到固定槽位、内嵌进内核镜像，并用 Shell 展示 fork/exec/wait 的多进程模型。

v0.3 的用户程序有三个：**shell**（交互式命令行）、**hello**（打印+睡眠演示）、**test**（汇编写的 syscall 冒烟测试）。本章代码对应最终版本 `user/` 目录全部文件：`shell.c`、`hello.c`、`test.S`、`user_link.ld`、`user_syscall.h`。

## 二、用户程序编译环境

用户程序与内核的编译环境完全不同：

| | 内核 | 用户程序 |
|---|---|---|
| 链接基址 | 0x40080000 | 0x48000000 |
| 运行级别 | EL1 | EL0 |
| 入口 | _start（汇编） | _start（C 或汇编） |
| 代码模型 | -fno-pic -fno-pie | -fno-pic -fno-pie |

用户程序只依赖 16 个系统调用封装（第 12 章），不链接任何库。`user/user_syscall.h` 是唯一的外部依赖。

**关于 `-fno-pic -fno-pie`**：这两个选项禁用位置无关代码（PIC）和位置无关可执行文件（PIE）。禁用后，GCC 对全局变量和函数的访问直接生成 PC 相对寻址指令（ADRP/ADD、BL），不经过 GOT（全局偏移表）。这让用户程序**不包含任何绝对地址**——代码被内核拷到哪个槽位都能正确运行（第 8 章）。这是 v0.3"用户程序可搬移加载"的编译基石。

## 三、用户链接脚本

```ld
/* 用户程序链接脚本：链接基址 0x48000000。
   实际加载槽位不同也没关系：代码使用 PC 相对寻址（ADRP/BL），
   入口 PC 和栈指针由内核按槽位基址重设。 */
ENTRY(_start)

SECTIONS {
    . = 0x48000000;

    .text : {
        *(.text.start)
        *(.text)
        *(.text.*)
    }

    . = ALIGN(8);
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    . = ALIGN(8);
    .data : {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
    }

    . = ALIGN(8);
    __bss_start = .;
    .bss : {
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
    }
    . = ALIGN(8);
    __bss_end = .;
}
```

**链接基址 0x48000000 只是名义基址**：真实运行时程序可能被内核拷到 8 个槽位中的任意一个（第 10 章）。`__bss_start/__bss_end` 导出给 C 程序，`_start` 里自己清 BSS。

## 四、用户程序如何启动

回顾第 10 章：`create_user_process` 把程序拷贝到槽位，在内核栈顶伪造异常帧（`elr_el1 = 槽位基址`、`spsr_el1 = 0`（EL0t）、`sp_el0 = 槽位顶`），调度后经 `interrupt_return` 执行 `restore_all + eret` 降入 EL0，从 `_start` 开始执行。

每个用户 C 程序的 `_start`：

```c
__attribute__((section(".text.start")))
void _start(void) {
    // 清零 BSS（子进程 fork 时不会重新运行这里，由内核拷贝父进程内存）
    for (char *p = __bss_start; p < __bss_end; p++) *p = 0;
    run();   // 或 shell 主循环
}
```

注意两点：

1. **`.text.start` 段**：确保 `_start` 在镜像开头（链接脚本把它放最前）。
2. **自己清 BSS**：没有 libc/crt0 帮你做，必须手动清零。注释说明了为什么 fork 后子进程不需要重新执行这里——内核在 fork 时拷贝的是父进程**已经初始化好**的内存。

## 五、用户态系统调用封装

`user/user_syscall.h` 提供全部 16 个系统调用的内联封装（真源）：

```c
// user/user_syscall.h -- 用户态系统调用封装（AArch64 svc）
#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stddef.h>

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

static inline long _syscall4(int nr, long a0, long a1, long a2, long a3) {
    return _syscall5(nr, a0, a1, a2, a3, 0);
}

static inline long _syscall1(int nr, long a0) {
    return _syscall3(nr, a0, 0, 0);
}

static inline long _syscall0(int nr) {
    return _syscall3(nr, 0, 0, 0);
}

static inline long sys_write(int fd, const void *buf, size_t count) {
    return _syscall3(SYS_write, fd, (long)buf, (long)count);
}

static inline long sys_read(int fd, void *buf, size_t count) {
    return _syscall3(SYS_read, fd, (long)buf, (long)count);
}

static inline long sys_exit(int code) {
    return _syscall1(SYS_exit, code);
}

static inline long sys_sleep(unsigned int ms) {
    return _syscall1(SYS_sleep, ms);
}

static inline long sys_getpid(void) {
    return _syscall0(SYS_getpid);
}

static inline long sys_fork(void) {
    return _syscall0(SYS_fork);
}

static inline long sys_execve(const char *name) {
    return _syscall1(SYS_execve, (long)name);
}

static inline long sys_open(const char *path, int flags) {
    return _syscall3(SYS_open, (long)path, flags, 0);
}

static inline long sys_close(long fd) {
    return _syscall1(SYS_close, fd);
}

static inline long sys_ls(const char *path) {
    return _syscall1(SYS_ls, (long)path);
}

static inline long sys_ps(void) {
    return _syscall0(SYS_ps);
}

static inline long sys_mkdir(const char *path) {
    return _syscall1(SYS_mkdir, (long)path);
}

static inline long sys_wait(void) {
    return _syscall0(SYS_wait);
}


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

#endif

```

约定（与 Linux AArch64 一致）：x8 = 调用号，x0-x2 = 参数，x0 = 返回值。`_syscall1/_syscall0` 只是 `_syscall3` 的缺参包装。

## 六、Shell：完整实现

### （一） 基础工具函数

```c
// user/shell.c -- 用户态 shell
#include "user_syscall.h"

extern char __bss_start[], __bss_end[];

static size_t slen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int scmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

static int sprefix(const char *s, const char *pre) {
    while (*pre) {
        if (*s++ != *pre++) return 0;
    }
    return 1;
}

static void print(const char *s) {
    sys_write(1, s, slen(s));
}

static void print_int(long v) {
    char buf[24];
    int i = 23;
    buf[i] = '\0';
    int neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    do { buf[--i] = '0' + (char)(u % 10); u /= 10; } while (u);
    if (neg) buf[--i] = '-';
    print(&buf[i]);
}
```

无 libc 环境下自给自足：字符串长度、比较、前缀匹配、整数转字符串全部手写。`print_int` 支持负数（处理 `v < 0` 的补码取反）。

### （二） 内置命令

```c
static void cmd_help(void) {
    print("Commands:\n");
    print("  help          - show this help\n");
    print("  echo <text>   - print text\n");
    print("  ps            - list processes\n");
    print("  ls [path]     - list files\n");
    print("  mkdir <path>  - create directory\n");
    print("  cat <file>    - print file content\n");
    print("  run hello     - fork+exec hello program\n");
    print("  run test      - fork+exec test program\n");
    print("  run net       - UDP DNS query demo\n");
    print("  pid           - print shell pid\n");
    print("  clear         - clear screen\n");
    print("  exit          - quit shell\n");
}

static void cmd_cat(const char *path) {
    long fd = sys_open(path, 0);
    if (fd < 0) {
        print("cat: cannot open ");
        print(path);
        print("\n");
        return;
    }
    char buf[129];
    for (;;) {
        long n = sys_read(fd, buf, 128);
        if (n <= 0) break;
        sys_write(1, buf, (size_t)n);
    }
    sys_close(fd);
}
```

`cat`：`sys_open(path, 0)`（O_RDONLY）→ 循环 `sys_read` 128 字节 → `sys_write` 到 stdout → `sys_close`。

### （三） run 命令：fork + exec + wait

这是 v0.3 最重要的用户态多进程展示（真源）：

```c
// fork + execve + wait，演示多进程
static void cmd_run(const char *prog) {
    long pid = sys_fork();
    if (pid < 0) {
        print("fork failed\n");
        return;
    }
    if (pid == 0) {
        if (sys_execve(prog) < 0) {
            print("exec: program not found: ");
            print(prog);
            print("\n");
            sys_exit(1);
        }
    }
    long code = sys_wait();
    print("[child pid=");
    print_int(pid);
    print(" exited with ");
    print_int(code);
    print("]\n");
}
```

流程：

1. `sys_fork()` 复制当前 shell 的现场（第 10 章）；
2. **子进程**（fork 返回 0）：`sys_execve(prog)` 用内嵌程序替换自己，失败则打印并 `sys_exit(1)`；成功则从新程序入口运行（不再回到这里）；
3. **父进程**：`sys_wait()` 阻塞等待子进程退出（第 11 章轮询），打印 `[child pid=N exited with M]`。

注意：子进程成功 exec 后不会执行 `sys_wait`（exec 成功后控制流已跳到新程序），只有 exec 失败的子进程才走到 `sys_exit(1)`。这是 v0.3 相对早期版本的一个正确性修正。

### （四） 命令分派

```c
static void execute(char *cmd) {
    // 去掉行首空格
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    // 去掉行尾换行/空格
    size_t len = slen(cmd);
    while (len > 0 && (cmd[len-1] == '\n' || cmd[len-1] == '\r' || cmd[len-1] == ' '))
        cmd[--len] = '\0';
    if (len == 0) return;

    if (scmp(cmd, "help") == 0) {
        cmd_help();
    } else if (sprefix(cmd, "echo ")) {
        print(cmd + 5);
        print("\n");
    } else if (scmp(cmd, "ps") == 0) {
        sys_ps();
    } else if (scmp(cmd, "ls") == 0) {
        sys_ls("/");
    } else if (sprefix(cmd, "ls ")) {
        if (sys_ls(cmd + 3) < 0) {
            print("ls: cannot open ");
            print(cmd + 3);
            print("\n");
        }
    } else if (sprefix(cmd, "mkdir ")) {
        if (sys_mkdir(cmd + 6) < 0) {
            print("mkdir failed\n");
        } else {
            print("created ");
            print(cmd + 6);
            print("\n");
        }
    } else if (sprefix(cmd, "cat ")) {
        cmd_cat(cmd + 4);
    } else if (sprefix(cmd, "run ")) {
        cmd_run(cmd + 4);
    } else if (scmp(cmd, "pid") == 0) {
        print("shell pid = ");
        print_int(sys_getpid());
        print("\n");
    } else if (scmp(cmd, "clear") == 0) {
        sys_write(1, "\033[2J\033[H", 7);
    } else if (scmp(cmd, "exit") == 0) {
        print("bye\n");
        sys_exit(0);
    } else {
        print("unknown command: ");
        print(cmd);
        print(" (try 'help')\n");
    }
}
```

无参数命令用 `scmp` 精确匹配，带参数命令用 `sprefix` 前缀匹配 + 偏移参数。`ls` 无参数时默认列根目录 `/`。

### （五） _start 与主循环

```c
__attribute__((section(".text.start")))
void _start(void) {
    // 清零 BSS（子进程 fork 时不会重新运行这里，由内核拷贝父进程内存）
    for (char *p = __bss_start; p < __bss_end; p++) *p = 0;

    print("\nMyOS Shell -- type 'help' for commands\n");

    char buf[256];
    for (;;) {
        print("$ ");
        long n = sys_read(0, buf, 255);
        if (n <= 0) continue;
        buf[n] = '\0';
        execute(buf);
    }
}
```

主循环：打印提示符 `$ ` → `sys_read(0, ...)` 读一行（控制台回显+退格，第 12 章）→ 截断成 C 字符串 → 执行。

## 七、hello：打印与睡眠演示

```c
// user/hello.c -- 用户态测试程序：打印、睡眠、退出
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

static void run(void) {
    print("Hello from user space!\n");
    print("my pid = ");
    print_int(sys_getpid());
    print("\n");

    for (int i = 1; i <= 3; i++) {
        print("working ... ");
        print_int(i);
        print("/3\n");
        sys_sleep(500);   // 期间 shell 可继续响应
    }

    print("Goodbye!\n");
    sys_exit(0);
}

__attribute__((section(".text.start")))
void _start(void) {
    for (char *p = __bss_start; p < __bss_end; p++) *p = 0;
    run();
}
```

hello 展示：打印进程号（`sys_getpid`）、循环 3 次"working ... i/3"（每次 `sys_sleep(500)` 毫秒）、`sys_exit(0)`。**睡眠期间 shell 父进程在 `sys_wait` 中轮询等待，整个系统仍响应时钟中断**——这直接证明了抢占调度与多进程模型。

## 八、test：汇编系统调用冒烟测试

```asm
# user/test.S -- 汇编用户程序：write + exit
.section .text.start
.global _start
_start:
    // write(1, msg, 13)
    mov x8, #1          // SYS_write
    mov x0, #1          // fd = stdout
    adr x1, msg
    mov x2, #13
    svc #0

    // exit(0)
    mov x8, #3          // SYS_exit
    mov x0, #0
    svc #0

    // 不会到达
    b .

.section .rodata
msg:
    .ascii "Hello, User!\n"
```

纯汇编验证两条系统调用（write/exit），不依赖任何 C 运行环境。**注意 `adr x1, msg` 是 PC 相对寻址**——消息字符串地址基于 PC 计算，程序搬到任何槽位都正确。这是"系统调用是用户态唯一入口"的最直白演示。

## 九、Makefile 构建链（v0.3 真源）

```makefile
# 用户程序
USER_PROGS = shell hello test net
USER_BINS = $(addprefix user/,$(addsuffix .bin,$(USER_PROGS)))

# 用户程序编译选项（PC 相对寻址，可加载到任意对齐槽位）
USER_CFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles \
              -mgeneral-regs-only -O0 -g -fno-pic -fno-pie
USER_LDFLAGS = -T user/user_link.ld -no-pie

# 用户程序先于 embed.o 构建
src/boot/embed.o: $(USER_BINS)

user/%.bin: user/%.elf
	$(OBJCOPY) -O binary $< $@

user/shell.elf: user/shell.c user/user_syscall.h user/user_link.ld
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/shell.c

user/hello.elf: user/hello.c user/user_syscall.h user/user_link.ld
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/hello.c

user/test.elf: user/test.S user/user_link.ld
	$(CC) -ffreestanding -nostdlib -nostartfiles -g $(USER_LDFLAGS) -o $@ user/test.S

user/net.elf: user/net.c user/user_syscall.h user/user_link.ld
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/net.c
```

依赖链：

```
shell.c ──► shell.elf ──► shell.bin ──► embed.o ──► kernel.elf
hello.c ──► hello.elf ──► hello.bin ──┘
test.S  ──► test.elf ──► test.bin ────┘
net.c   ──► net.elf  ──► net.bin  ────┘
```

构建链的三个特点值得注意：C 程序一步编译链接——`$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/shell.c` 直接产出 ELF，不经过中间 `.o`（依赖声明在目标里，源文件变化会触发重建）；`-no-pie` 与 `-fno-pie` 配套，告诉链接器不要生成 PIE 可执行文件；`src/boot/embed.o: $(USER_BINS)` 强制用户程序二进制先于 `embed.o` 构建（`.incbin` 需要 bin 存在）。三步配合，用户程序从源码到内嵌进内核镜像一气呵成。

## 十、内嵌程序表与查找

第 5 章的 embed.S 导出 `shell_prog_start/end` 等符号，`proc.c` 维护查找表：

```c
// src/proc/proc.c
static const struct embedded_prog embedded_progs[] = {
    { "shell", shell_prog_start, shell_prog_end },
    { "hello", hello_prog_start, hello_prog_end },
    { "test",  test_prog_start,  test_prog_end  },
    { "net",   net_prog_start,   net_prog_end   },
    { 0, 0, 0 },
};

void *user_prog_find(const char *name, unsigned long *size) {
    for (int i = 0; embedded_progs[i].name; i++) {
        if (strcmp(embedded_progs[i].name, name) == 0) {
            if (size)
                *size = (unsigned long)(embedded_progs[i].end - embedded_progs[i].start);
            return (void *)embedded_progs[i].start;
        }
    }
    return NULL;
}
```

`sys_execve(name)` → `user_prog_find(name, &size)` → 拷到当前槽位。内嵌表让"可执行程序"与内核镜像同生共死，无需文件系统。

## 十一、完整交互演示

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
  run net       - UDP DNS query demo
  pid           - print shell pid
  clear         - clear screen
  exit          - quit shell
$ echo hello world
hello world
$ cat /etc/motd
Welcome to MyOS! Type 'help' for commands.
$ run hello
Hello from user space!
my pid = 2
working ... 1/3
working ... 2/3
working ... 3/3
Goodbye!
[child pid=2 exited with 0]
$ run test
Hello, User!
[child pid=3 exited with 0]
$ run net
[net] UDP DNS query demo (10.0.2.3:53)
[net] querying example.com -> 29 bytes sent
[net] example.com = 104.20.23.154
[net] done
[child pid=4 exited with 0]
$ pid
shell pid = 1
$ ps
PID  NAME        STATE      PRIO
    1shell       RUNNING        10
$ exit
bye
```

## 十二、小结

本章实现的拼图把用户空间完整落地：用户程序编译/链接环境（`-fno-pic` 的 PC 相对寻址 + 槽位无关）；16 个系统调用封装（`user_syscall.h`，`_syscall3`/`_syscall5` 内联复用）；shell 的 11 条命令与交互式命令行（回显/退格）；`run` 命令的 fork + exec + wait 前台模型（v0.3 修正了 exec 成功分支——成功后子进程不 wait、直接跑新程序）；hello 的打印/睡眠演示与 test 的汇编冒烟测试；`net` 的 UDP DNS 查询演示（第 12 章三个网络系统调用的真实调用方）；Makefile 构建链（C/asm → elf → bin → 内嵌）。从这一章起，内核的"用户可见形态"第一次完整呈现。

## 十三、练习

练习一，给 shell 加 `run test` 之外的 `which` 命令，遍历内嵌程序表打印可用程序名——理解 `user_prog_find` 的表扫描机制。练习二，写一个 `user/loop.c`：无限循环打印并 `sys_sleep`，观察抢占调度下 shell 是否保持响应——这是对第 11 章调度模型的直观验证。练习三，修改 `cmd_run` 支持 `&` 后台运行（父进程不 wait）——思考后台子进程退出后由谁回收，这会把第 11 章的孤儿进程处理机制变成真实需求。练习四，给 `run net` 增加第二个参数（如 `run net baidu.com`），用 `sprefix` 提取域名传给 `build_dns_query`——域名解析参数化后，可以进一步验证"任意域名都能拿到真实 A 记录"，体会网络系统调用的通用性。
