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
