// src/kernel/net/net.c
#include "net.h"
#include "virtio_net.h"
#include "uart.h"

extern void arp_init(void);
extern void arp_rx(struct arphdr *arp);
extern void ip_init(void);
extern void ip_rx(struct iphdr *ip, size_t total_len);
extern void icmp_init(void);
extern void udp_init(void);

void net_init(void) {
    virtio_net_init();
    arp_init();
    ip_init();
    icmp_init();
    udp_init();
    uart_puts("net: protocol stack initialized\n");
}

// 网络接收处理入口
void net_rx_handler(uint8_t *data, size_t len) {
    if (len < sizeof(struct ethhdr)) {
        return;
    }

    struct ethhdr *eth = (struct ethhdr *)data;
    uint16_t proto = __builtin_bswap16(eth->h_proto);

    switch (proto) {
        case ETH_TYPE_ARP:
            arp_rx((struct arphdr *)(data + sizeof(struct ethhdr)));
            break;
        case ETH_TYPE_IPV4:
            ip_rx((struct iphdr *)(data + sizeof(struct ethhdr)),
                  len - sizeof(struct ethhdr));
            break;
        default:
            // 不支持的协议，忽略
            break;
    }
}
