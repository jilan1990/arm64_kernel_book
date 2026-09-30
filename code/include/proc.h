// include/proc.h
#ifndef PROC_H
#define PROC_H

#include <stddef.h>
#include "exception.h"

#define MAX_PROCS 64
#define PROC_NAME_LEN 32

// 用户程序加载区域：位于物理页分配器管理的 128MB（0x40000000~0x48000000）
// 之外、256MB RAM 之内，避免与内核页分配器冲突。
// 每个用户进程占一个 1MB 槽位：低处放代码，栈从槽顶向下生长。
#define USER_REGION_BASE 0x48000000UL
#define USER_SLOT_SIZE   0x100000UL
#define USER_SLOTS       8
#define USER_PROG_MAX    0xF0000UL

// 进程状态
enum proc_state {
    PROC_UNUSED = 0,
    PROC_RUNNING,
    PROC_READY,
    PROC_SLEEPING,
    PROC_ZOMBIE,
};

// CPU 上下文：callee-saved 寄存器 + 返回地址 + 栈指针
struct cpu_context {
    unsigned long x19;
    unsigned long x20;
    unsigned long x21;
    unsigned long x22;
    unsigned long x23;
    unsigned long x24;
    unsigned long x25;
    unsigned long x26;
    unsigned long x27;
    unsigned long x28;
    unsigned long x29;
    unsigned long x30;
    unsigned long sp;
};

// 进程控制块
struct task_struct {
    int pid;
    int parent_pid;
    char name[PROC_NAME_LEN];
    enum proc_state state;
    int priority;                  // 数值越小优先级越高
    unsigned long stack;           // 内核栈页（回收时释放）
    unsigned long code_base;       // 用户程序加载地址（0 表示内核线程）
    unsigned long prog_size;       // 用户程序大小（fork 时拷贝）
    struct cpu_context context;
    unsigned long wakeup_tick;     // SLEEPING：到期 tick；~0 表示等信号量
    struct task_struct *wait_next; // 等待队列链接
    int exit_code;
};

void proc_init(void);
struct task_struct *create_kernel_thread(const char *name, void (*entry)(void), int priority);
struct task_struct *create_user_process(const char *name, const void *data,
                                        unsigned long size, int priority);
long proc_fork(struct exception_frame *frame);
long proc_execve(struct exception_frame *frame, const char *name);
void schedule(void);
void context_switch(struct cpu_context *old, struct cpu_context *new);
void switch_to(struct cpu_context *new_ctx);
void preempt_from_frame(struct exception_frame *frame);
void yield(void);
void proc_tick(void);
void proc_reap(void);
void ps_dump(void);
void *user_prog_find(const char *name, unsigned long *size);

extern struct task_struct proc_table[MAX_PROCS];
extern struct task_struct *current;
extern volatile int need_resched;

// 内嵌用户程序（由 src/boot/embed.S 提供）
extern const char shell_prog_start[], shell_prog_end[];
extern const char hello_prog_start[], hello_prog_end[];
extern const char test_prog_start[], test_prog_end[];
extern const char net_prog_start[], net_prog_end[];

#endif
