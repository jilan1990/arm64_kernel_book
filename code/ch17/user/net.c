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
