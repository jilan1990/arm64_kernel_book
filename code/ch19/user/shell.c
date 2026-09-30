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
