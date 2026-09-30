// src/kernel/net/arp.c
#include "net.h"
#include "virtio_net.h"
#include "uart.h"
#include "string.h"

#define ARP_CACHE_SIZE 16

struct arp_entry {
    uint32_t ip;
    uint8_t  mac[ETH_ALEN];
    int      valid;
};

static struct arp_entry arp_cache[ARP_CACHE_SIZE];

void arp_init(void) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].valid = 0;
    }
}

static struct arp_entry *arp_find(uint32_t ip) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            return &arp_cache[i];
        }
    }
    return NULL;
}

static void arp_add(uint32_t ip, const uint8_t *mac) {
    struct arp_entry *entry = arp_find(ip);
    if (!entry) {
        // 找一个空槽位
        for (int i = 0; i < ARP_CACHE_SIZE; i++) {
            if (!arp_cache[i].valid) {
                entry = &arp_cache[i];
                break;
            }
        }
        if (!entry) entry = &arp_cache[0];  // 满了就覆盖第一个
    }
    entry->ip = ip;
    memcpy(entry->mac, mac, ETH_ALEN);
    entry->valid = 1;
}

// 发送 ARP 请求
static void arp_send_request(uint32_t target_ip) {
    uint8_t frame[sizeof(struct ethhdr) + sizeof(struct arphdr)];
    struct ethhdr *eth = (struct ethhdr *)frame;
    struct arphdr *arp = (struct arphdr *)(frame + sizeof(struct ethhdr));

    uint8_t *mac = virtio_net_get_mac();

    // 以太网头部：广播
    memset(eth->h_dest, 0xFF, ETH_ALEN);
    memcpy(eth->h_source, mac, ETH_ALEN);
    eth->h_proto = __builtin_bswap16(ETH_TYPE_ARP);

    // ARP 请求
    arp->ar_hrd = __builtin_bswap16(1);          // 以太网
    arp->ar_pro = __builtin_bswap16(ETH_TYPE_IPV4);
    arp->ar_hln = ETH_ALEN;
    arp->ar_pln = IP_ALEN;
    arp->ar_op  = __builtin_bswap16(ARP_OP_REQUEST);
    memcpy(arp->ar_sha, mac, ETH_ALEN);
    arp->ar_sip = __builtin_bswap32(NET_IP_ADDR);
    memset(arp->ar_tha, 0, ETH_ALEN);
    arp->ar_tip = __builtin_bswap32(target_ip);

    virtio_net_send(frame, sizeof(frame));
}

// 处理收到的 ARP 包
void arp_rx(struct arphdr *arp) {
    uint16_t op = __builtin_bswap16(arp->ar_op);

    // 不管是请求还是响应，都把发送端的 IP-MAC 加入缓存
    arp_add(__builtin_bswap32(arp->ar_sip), arp->ar_sha);

    if (op == ARP_OP_REQUEST && __builtin_bswap32(arp->ar_tip) == NET_IP_ADDR) {
        // 这是发给我们的 ARP 请求，发送 ARP 响应
        uint8_t frame[sizeof(struct ethhdr) + sizeof(struct arphdr)];
        struct ethhdr *eth = (struct ethhdr *)frame;
        struct arphdr *reply = (struct arphdr *)(frame + sizeof(struct ethhdr));
        uint8_t *mac = virtio_net_get_mac();

        memcpy(eth->h_dest, arp->ar_sha, ETH_ALEN);
        memcpy(eth->h_source, mac, ETH_ALEN);
        eth->h_proto = __builtin_bswap16(ETH_TYPE_ARP);

        reply->ar_hrd = __builtin_bswap16(1);
        reply->ar_pro = __builtin_bswap16(ETH_TYPE_IPV4);
        reply->ar_hln = ETH_ALEN;
        reply->ar_pln = IP_ALEN;
        reply->ar_op  = __builtin_bswap16(ARP_OP_REPLY);
        memcpy(reply->ar_sha, mac, ETH_ALEN);
        reply->ar_sip = __builtin_bswap32(NET_IP_ADDR);
        memcpy(reply->ar_tha, arp->ar_sha, ETH_ALEN);
        reply->ar_tip = arp->ar_sip;

        virtio_net_send(frame, sizeof(frame));
    }
}

// 解析 IP 地址为 MAC 地址，未命中时发送 ARP 请求并返回 0
uint32_t resolve_arp(uint32_t ip) {
    struct arp_entry *entry = arp_find(ip);
    if (entry) {
        return (uint32_t)(uintptr_t)entry->mac;  // 返回 MAC 地址指针
    }
    arp_send_request(ip);
    return 0;  // 未命中，需要等待 ARP 响应
}
