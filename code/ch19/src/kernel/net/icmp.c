// src/kernel/net/icmp.c
#include "net.h"
#include "uart.h"
#include "string.h"

extern int ip_send(uint32_t dst_ip, uint8_t protocol, const uint8_t *data, size_t len);

void icmp_init(void) {
    // 无需初始化
}

// 处理收到的 ICMP 包
void icmp_rx(struct iphdr *ip, uint8_t *data, size_t len) {
    if (len < sizeof(struct icmphdr)) {
        return;
    }

    struct icmphdr *icmp = (struct icmphdr *)data;

    if (icmp->type == ICMP_TYPE_ECHO_REQUEST) {
        // 收到 ping 请求，回复 ping 响应
        uint32_t src_ip = __builtin_bswap32(ip->saddr);

        uart_puts("icmp: echo request from ");
        uart_puthex((src_ip >> 24) & 0xFF);
        uart_putc('.');
        uart_puthex((src_ip >> 16) & 0xFF);
        uart_putc('.');
        uart_puthex((src_ip >> 8) & 0xFF);
        uart_putc('.');
        uart_puthex(src_ip & 0xFF);
        uart_puts("\n");

        // 构造 Echo Reply：类型改为 0，校验和重新计算
        icmp->type = ICMP_TYPE_ECHO_REPLY;
        icmp->checksum = 0;
        icmp->checksum = ip_checksum(icmp, len);

        // 交换源和目的 IP 发送
        ip_send(src_ip, IP_PROTO_ICMP, data, len);
    }
}
