// src/kernel/syscall.c
#include "syscall.h"
#include "uart.h"
#include "proc.h"
#include "timer.h"
#include "vfs.h"
#include "mm.h"
#include "udp.h"
#include "virtio_net.h"

static syscall_handler_t syscall_table[MAX_SYSCALL];

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

static long sys_fork(struct exception_frame *frame) {
    return proc_fork(frame);
}

static long sys_execve(struct exception_frame *frame) {
    return proc_execve(frame, (const char *)frame->x0);
}

static long sys_sleep(struct exception_frame *frame) {
    unsigned long ms = frame->x0;
    unsigned long ticks = (ms * timer_get_hz() + 999) / 1000;
    if (ticks == 0) ticks = 1;
    current->wakeup_tick = get_ticks() + ticks;
    current->state = PROC_SLEEPING;
    schedule();            // 睡眠期间让出 CPU
    return 0;
}

static long sys_getpid(struct exception_frame *frame) {
    (void)frame;
    return current->pid;
}

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

static long sys_ps(struct exception_frame *frame) {
    (void)frame;
    ps_dump();
    return 0;
}

static long sys_mkdir(struct exception_frame *frame) {
    return vfs_mkdir((const char *)frame->x0);
}

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

// 网络：创建 UDP socket 并绑定本地端口，返回 sockfd
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
    syscall_table[SYS_net_socket] = sys_net_socket;
    syscall_table[SYS_net_send]   = sys_net_send;
    syscall_table[SYS_net_recv]   = sys_net_recv;
    uart_puts("syscall table initialized\n");
}

void handle_syscall(struct exception_frame *frame) {
    int nr = (int)frame->x8;

    if (nr < 0 || nr >= MAX_SYSCALL || !syscall_table[nr]) {
        frame->x0 = -1;
        return;
    }
    frame->x0 = syscall_table[nr](frame);
}
