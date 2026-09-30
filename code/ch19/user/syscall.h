// user/syscall.h
#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stddef.h>

#define SYS_write   1
#define SYS_read    2
#define SYS_exit    3
#define SYS_sleep   6
#define SYS_getpid  7

static inline long syscall0(int nr) {
    register long x8 asm("x8") = nr;
    register long x0 asm("x0");
    asm volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline long syscall1(int nr, long a1) {
    register long x8 asm("x8") = nr;
    register long x0 asm("x0") = a1;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline long syscall3(int nr, long a1, long a2, long a3) {
    register long x8 asm("x8") = nr;
    register long x0 asm("x0") = a1;
    register long x1 asm("x1") = a2;
    register long x2 asm("x2") = a3;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static inline long write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_write, fd, (long)buf, count);
}

static inline long read(int fd, void *buf, size_t count) {
    return syscall3(SYS_read, fd, (long)buf, count);
}

static inline void exit(int code) {
    syscall1(SYS_exit, code);
    while (1);  // 不会到达
}

static inline void sleep(unsigned int ms) {
    syscall1(SYS_sleep, ms);
}

static inline int getpid(void) {
    return (int)syscall0(SYS_getpid);
}

#endif
