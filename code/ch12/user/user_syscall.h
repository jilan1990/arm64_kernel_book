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
