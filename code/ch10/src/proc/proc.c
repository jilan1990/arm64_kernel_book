// src/proc/proc.c
// 进程管理：进程表、进程创建、调度器（抢占 + 睡眠唤醒 + 僵尸回收）、fork/exec
#include "proc.h"
#include "mm.h"
#include "uart.h"
#include "slab.h"
#include "string.h"
#include "timer.h"

struct task_struct proc_table[MAX_PROCS];
struct task_struct *current = NULL;
volatile int need_resched = 0;

static int next_pid = 1;

// exception.S：从当前栈上的异常帧 eret 返回
extern void interrupt_return(void);

// 内嵌用户程序表
struct embedded_prog {
    const char *name;
    const char *start;
    const char *end;
};

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

void proc_init(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        proc_table[i].state = PROC_UNUSED;
        proc_table[i].pid = 0;
        proc_table[i].parent_pid = 0;
        proc_table[i].stack = 0;
        proc_table[i].code_base = 0;
        proc_table[i].prog_size = 0;
        proc_table[i].wait_next = NULL;
        proc_table[i].wakeup_tick = 0;
    }

    // 0 号进程：idle（优先级数值最大 = 最不优先）
    struct task_struct *idle = &proc_table[0];
    idle->pid = 0;
    idle->state = PROC_RUNNING;
    idle->priority = 1000;
    idle->stack = 0;
    strcpy(idle->name, "idle");
    current = idle;

    uart_puts("Process manager initialized\n");
}

static struct task_struct *alloc_task_slot(void) {
    for (int i = 1; i < MAX_PROCS; i++)
        if (proc_table[i].state == PROC_UNUSED) return &proc_table[i];
    return NULL;
}

static void setup_task(struct task_struct *p, const char *name, int priority) {
    p->pid = next_pid++;
    p->parent_pid = current->pid;
    strncpy(p->name, name, PROC_NAME_LEN - 1);
    p->name[PROC_NAME_LEN - 1] = '\0';
    p->priority = priority;
    p->wait_next = NULL;
    p->exit_code = 0;
    p->wakeup_tick = 0;
}

// 内核线程：切换后从 entry 开始执行（EL1）
struct task_struct *create_kernel_thread(const char *name, void (*entry)(void), int priority) {
    struct task_struct *p = alloc_task_slot();
    if (!p) return NULL;
    void *stack = alloc_page();
    if (!stack) return NULL;

    setup_task(p, name, priority);
    p->stack = (unsigned long)stack;
    p->state = PROC_READY;

    memset(&p->context, 0, sizeof(p->context));
    p->context.sp = (unsigned long)stack + PAGE_SIZE;
    p->context.x30 = (unsigned long)entry;
    return p;
}

// 用户进程：程序拷贝到自己的槽位，在内核栈顶伪造异常帧，eret 进入 EL0
struct task_struct *create_user_process(const char *name, const void *data,
                                        unsigned long size, int priority) {
    if (!data || size == 0 || size > USER_PROG_MAX) return NULL;

    struct task_struct *p = alloc_task_slot();
    if (!p) return NULL;
    void *stack = alloc_page();
    if (!stack) return NULL;

    int slot = (int)((p - proc_table) % USER_SLOTS);
    unsigned long base = USER_REGION_BASE + (unsigned long)slot * USER_SLOT_SIZE;
    memcpy((void *)base, data, size);

    setup_task(p, name, priority);
    p->stack = (unsigned long)stack;
    p->code_base = base;
    p->prog_size = size;
    p->state = PROC_READY;

    struct exception_frame *frame = (struct exception_frame *)
        ((unsigned long)stack + PAGE_SIZE - sizeof(struct exception_frame));
    memset(frame, 0, sizeof(*frame));
    frame->elr_el1 = base;
    frame->spsr_el1 = 0;              // EL0t，中断开启
    frame->sp_el0 = base + USER_SLOT_SIZE;

    memset(&p->context, 0, sizeof(p->context));
    p->context.sp = (unsigned long)frame;
    p->context.x30 = (unsigned long)interrupt_return;
    return p;
}

// fork：复制父进程程序到子进程槽位，复制异常帧和已用的用户栈，子进程返回 0。
// 用户代码用 ADRP 等 PC 相对寻址，拷贝到对齐的槽位后可正确运行。
long proc_fork(struct exception_frame *frame) {
    if (!current->code_base) return -1;   // 只支持用户进程

    struct task_struct *p = alloc_task_slot();
    if (!p) return -1;
    void *stack = alloc_page();
    if (!stack) return -1;

    int slot = (int)((p - proc_table) % USER_SLOTS);
    unsigned long base = USER_REGION_BASE + (unsigned long)slot * USER_SLOT_SIZE;
    memcpy((void *)base, (void *)current->code_base, current->prog_size);

    // 复制父进程已使用的用户栈（从 sp 到槽顶）
    unsigned long top = current->code_base + USER_SLOT_SIZE;
    unsigned long sp = frame->sp_el0;
    if (sp < current->code_base || sp > top) sp = top;
    unsigned long used = top - sp;
    if (used > USER_SLOT_SIZE / 2) used = USER_SLOT_SIZE / 2;
    if (used)
        memcpy((void *)(base + USER_SLOT_SIZE - used),
               (void *)(top - used), used);

    struct exception_frame *cframe = (struct exception_frame *)
        ((unsigned long)stack + PAGE_SIZE - sizeof(struct exception_frame));
    memcpy(cframe, frame, sizeof(*cframe));

    unsigned long delta = base - current->code_base;
    cframe->elr_el1 += delta;
    cframe->sp_el0  += delta;
    cframe->x0 = 0;                       // 子进程 fork 返回 0

    setup_task(p, current->name, current->priority);
    p->stack = (unsigned long)stack;
    p->code_base = base;
    p->prog_size = current->prog_size;
    p->state = PROC_READY;

    memset(&p->context, 0, sizeof(p->context));
    p->context.sp = (unsigned long)cframe;
    p->context.x30 = (unsigned long)interrupt_return;
    return p->pid;
}

// execve：用内嵌程序替换当前进程的代码，重置入口和栈
long proc_execve(struct exception_frame *frame, const char *name) {
    if (!current->code_base || !name) return -1;

    unsigned long size = 0;
    void *data = user_prog_find(name, &size);
    if (!data) return -1;

    memcpy((void *)current->code_base, data, size);
    current->prog_size = size;
    strncpy(current->name, name, PROC_NAME_LEN - 1);
    current->name[PROC_NAME_LEN - 1] = '\0';

    frame->elr_el1 = current->code_base;
    frame->sp_el0  = current->code_base + USER_SLOT_SIZE;
    frame->x0 = 0;
    return 0;
}

// 选择就绪进程中优先级最高（数值最小）的
static struct task_struct *pick_next(void) {
    struct task_struct *best = NULL;
    for (int i = 1; i < MAX_PROCS; i++) {
        struct task_struct *p = &proc_table[i];
        if (p->state == PROC_READY && (!best || p->priority < best->priority))
            best = p;
    }
    return best;
}

// 内核态主动调度（进程退出、睡眠等场景）
void schedule(void) {
    struct task_struct *next = pick_next();
    if (!next) next = &proc_table[0];   // 无就绪进程则回 idle
    if (next == current) return;

    struct task_struct *prev = current;
    next->state = PROC_RUNNING;
    current = next;
    context_switch(&prev->context, &next->context);
}

// 时钟中断抢占：把中断现场写入 prev 的上下文后切换。
// prev 再次被调度时经 interrupt_return 从异常帧 eret 恢复。
void preempt_from_frame(struct exception_frame *frame) {
    struct task_struct *next = pick_next();
    if (!next || next == current) return;
    if (next->priority > current->priority) return;   // 不抢占更低优先级

    struct task_struct *prev = current;
    prev->context.x19 = frame->x19;
    prev->context.x20 = frame->x20;
    prev->context.x21 = frame->x21;
    prev->context.x22 = frame->x22;
    prev->context.x23 = frame->x23;
    prev->context.x24 = frame->x24;
    prev->context.x25 = frame->x25;
    prev->context.x26 = frame->x26;
    prev->context.x27 = frame->x27;
    prev->context.x28 = frame->x28;
    prev->context.x29 = frame->x29;
    prev->context.x30 = (unsigned long)interrupt_return;
    prev->context.sp  = (unsigned long)frame;

    next->state = PROC_RUNNING;
    current = next;
    switch_to(&next->context);
}

void yield(void) {
    schedule();
}

// 每个时钟 tick：唤醒到期进程、请求抢占
void proc_tick(void) {
    unsigned long now = get_ticks();
    for (int i = 1; i < MAX_PROCS; i++) {
        struct task_struct *p = &proc_table[i];
        if (p->state == PROC_SLEEPING && p->wakeup_tick <= now)
            p->state = PROC_READY;
    }
    if (pick_next()) need_resched = 1;
}

// 回收僵尸进程（idle 循环中调用）。
// 父进程仍存活且在等待时由 sys_wait 回收，这里跳过。
void proc_reap(void) {
    for (int i = 1; i < MAX_PROCS; i++) {
        struct task_struct *p = &proc_table[i];
        if (p->state != PROC_ZOMBIE) continue;

        int parent_alive = 0;
        if (p->parent_pid > 0) {
            for (int j = 0; j < MAX_PROCS; j++) {
                struct task_struct *q = &proc_table[j];
                if (q != p && q->pid == p->parent_pid && q->state != PROC_UNUSED) {
                    parent_alive = 1;
                    break;
                }
            }
        }
        if (parent_alive) continue;

        uart_puts("[reap] ");
        uart_puts(p->name);
        uart_puts(" pid=");
        uart_puthex((unsigned int)p->pid);
        uart_puts(" exit_code=");
        uart_puthex((unsigned int)p->exit_code);
        uart_puts("\n");
        if (p->stack) free_page((void *)p->stack);
        p->state = PROC_UNUSED;
        p->pid = 0;
    }
}

// 打印进程表（sys_ps）
static void print_fixed(const char *s, int width) {
    uart_puts(s);
    for (int i = (int)strlen(s); i < width; i++) uart_putc(' ');
}

static void print_dec_pad(int v, int width) {
    char buf[16];
    int pos = 15;
    buf[pos] = '\0';
    do { buf[--pos] = '0' + v % 10; v /= 10; } while (v);
    print_fixed(&buf[pos], width);
}

void ps_dump(void) {
    static const char *state_names[] = {
        "UNUSED", "RUNNING", "READY", "SLEEPING", "ZOMBIE"
    };
    uart_puts("PID  NAME        STATE      PRIO\n");
    for (int i = 0; i < MAX_PROCS; i++) {
        struct task_struct *p = &proc_table[i];
        if (p->state == PROC_UNUSED) continue;
        print_dec_pad(p->pid, 5);
        print_fixed(p->name, 12);
        print_fixed(state_names[p->state], 11);
        print_dec_pad(p->priority, 5);
        uart_puts("\n");
    }
}

void device_init(void) {
    extern void ramdisk_init(void);
    extern void uart_char_init(void);
    ramdisk_init();
    uart_char_init();
}

// 创建第一个用户进程（内嵌 shell）
void create_test_processes(void) {
    unsigned long size = 0;
    void *data = user_prog_find("shell", &size);
    if (!data) {
        uart_puts("create_test_processes: no embedded shell!\n");
        return;
    }
    uart_puts("embedded shell: ");
    uart_puthex((unsigned int)size);
    uart_puts(" bytes\n");
    create_user_process("shell", data, size, 10);
}
