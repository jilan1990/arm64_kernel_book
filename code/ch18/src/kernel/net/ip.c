// src/kernel/net/ip.c
#include "net.h"
#include "virtio_net.h"
#include "uart.h"
#include "string.h"
#include "slab.h"

extern void icmp_rx(struct iphdr *ip, uint8_t *data, size_t len);
extern void udp_rx(struct iphdr *ip, uint8_t *data, size_t len);

static uint16_t ip_id_counter = 0;

void ip_init(void) {
    ip_id_counter = 0;
}

// 处理收到的 IP 包
void ip_rx(struct iphdr *ip, size_t total_len) {
    // 验证版本和 IHL
    uint8_t version = ip->ihl_version >> 4;
    uint8_t ihl = ip->ihl_version & 0x0F;
    if (version != 4 || ihl < 5) {
        return;  // 不是 IPv4 或头部太短
    }

    size_t header_len = ihl * 4;
    if (total_len < header_len) {
        return;
    }

    // 验证校验和
    uint16_t old_check = ip->check;
    ip->check = 0;
    uint16_t calc_check = ip_checksum(ip, header_len);
    ip->check = old_check;
    if (old_check != calc_check) {
        uart_puts("ip: checksum mismatch\n");
        return;
    }

    // 检查目的 IP 是否是我们的地址或广播
    uint32_t daddr = __builtin_bswap32(ip->daddr);
    if (daddr != NET_IP_ADDR && daddr != 0xFFFFFFFF) {
        return;  // 不是发给我们的
    }

    uint8_t *payload = (uint8_t *)ip + header_len;
    size_t payload_len = __builtin_bswap16(ip->tot_len) - header_len;

    // 根据协议分发
    switch (ip->protocol) {
        case IP_PROTO_ICMP:
            icmp_rx(ip, payload, payload_len);
            break;
        case IP_PROTO_UDP:
            udp_rx(ip, payload, payload_len);
            break;
        default:
            // 不支持的协议，忽略
            break;
    }
}

// 发送 IP 包
int ip_send(uint32_t dst_ip, uint8_t protocol, const uint8_t *data, size_t len) {
    size_t total_len = sizeof(struct iphdr) + len;
    uint8_t *packet = kmalloc(total_len);  // 假设已有 kmalloc
    if (!packet) return -1;

    struct iphdr *ip = (struct iphdr *)packet;
    ip->ihl_version = (4 << 4) | 5;  // IPv4, IHL=5
    ip->tos = 0;
    ip->tot_len = __builtin_bswap16(total_len);
    ip->id = __builtin_bswap16(ip_id_counter++);
    ip->frag_off = 0;  // 不分片
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->check = 0;
    ip->saddr = __builtin_bswap32(NET_IP_ADDR);
    ip->daddr = __builtin_bswap32(dst_ip);
    ip->check = ip_checksum(ip, sizeof(struct iphdr));

    memcpy(packet + sizeof(struct iphdr), data, len);

    // 确定下一跳：同一子网直接发送，否则发送到网关
    uint32_t next_hop;
    if ((dst_ip & NET_NETMASK) == (NET_IP_ADDR & NET_NETMASK)) {
        next_hop = dst_ip;
    } else {
        next_hop = NET_GATEWAY;
    }

    // 通过 ARP 获取下一跳的 MAC 地址：
    // 未命中时发送 ARP 请求，轮询 RX 队列等待响应（最多约 2 秒）
    uint8_t *mac = NULL;
    for (int attempt = 0; attempt < 40 && !mac; attempt++) {
        mac = (uint8_t *)resolve_arp(next_hop);
        if (mac) break;
        virtio_net_poll();                    // 处理收到的 ARP 响应
        for (volatile int i = 0; i < 50000; i++);  // 短暂延迟
    }
    if (!mac) {
        kfree(packet);  // ARP 未命中，丢弃
        return -1;
    }

    // 封装以太网帧并发送
    size_t frame_len = sizeof(struct ethhdr) + total_len;
    uint8_t *frame = kmalloc(frame_len);
    if (!frame) {
        kfree(packet);
        return -1;
    }

    struct ethhdr *eth = (struct ethhdr *)frame;
    memcpy(eth->h_dest, mac, ETH_ALEN);
    memcpy(eth->h_source, virtio_net_get_mac(), ETH_ALEN);
    eth->h_proto = __builtin_bswap16(ETH_TYPE_IPV4);
    memcpy(frame + sizeof(struct ethhdr), packet, total_len);

    virtio_net_send(frame, frame_len);

    kfree(packet);
    kfree(frame);
    return len;
}
