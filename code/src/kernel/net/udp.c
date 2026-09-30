// src/kernel/net/udp.c
#include "udp.h"
#include "uart.h"
#include "string.h"
#include "slab.h"

extern int ip_send(uint32_t dst_ip, uint8_t protocol, const uint8_t *data, size_t len);

static struct udp_socket sockets[UDP_MAX_SOCKETS];

void udp_init(void) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        sockets[i].used = 0;
        sockets[i].rx_len = 0;
    }
}

// 处理收到的 UDP 包
void udp_rx(struct iphdr *ip, uint8_t *data, size_t len) {
    if (len < sizeof(struct udphdr)) {
        return;
    }

    struct udphdr *udp = (struct udphdr *)data;
    uint16_t dst_port = __builtin_bswap16(udp->dest);
    uint16_t src_port = __builtin_bswap16(udp->source);
    uint32_t src_ip = __builtin_bswap32(ip->saddr);

    // 查找绑定到该端口的 socket
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (sockets[i].used && sockets[i].local_port == dst_port) {
            // 把数据放入接收缓冲区
            uint8_t *payload = data + sizeof(struct udphdr);
            size_t payload_len = len - sizeof(struct udphdr);
            if (payload_len > UDP_RX_BUF_SIZE) {
                payload_len = UDP_RX_BUF_SIZE;
            }
            memcpy(sockets[i].rx_buf, payload, payload_len);
            sockets[i].rx_len = payload_len;
            sockets[i].remote_ip = src_ip;
            sockets[i].remote_port = src_port;
            return;
        }
    }
    // 没有找到对应的 socket，丢弃数据包
}

int udp_socket(void) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!sockets[i].used) {
            sockets[i].used = 1;
            sockets[i].local_port = 0;
            sockets[i].rx_len = 0;
            return i;
        }
    }
    return -1;
}

int udp_bind(int sockfd, uint16_t port) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS || !sockets[sockfd].used) {
        return -1;
    }
    sockets[sockfd].local_port = port;
    return 0;
}

int udp_sendto(int sockfd, const uint8_t *data, size_t len,
               uint32_t dst_ip, uint16_t dst_port) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS || !sockets[sockfd].used) {
        return -1;
    }

    size_t total_len = sizeof(struct udphdr) + len;
    uint8_t *packet = kmalloc(total_len);
    if (!packet) return -1;

    struct udphdr *udp = (struct udphdr *)packet;
    udp->source = __builtin_bswap16(sockets[sockfd].local_port);
    udp->dest = __builtin_bswap16(dst_port);
    udp->len = __builtin_bswap16(total_len);
    udp->check = 0;  // IPv4 中 UDP 校验和可选，设为 0 表示不校验

    memcpy(packet + sizeof(struct udphdr), data, len);

    int ret = ip_send(dst_ip, IP_PROTO_UDP, packet, total_len);
    kfree(packet);
    return ret > 0 ? len : -1;
}

int udp_recvfrom(int sockfd, uint8_t *buf, size_t max_len,
                  uint32_t *src_ip, uint16_t *src_port) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS || !sockets[sockfd].used) {
        return -1;
    }

    if (sockets[sockfd].rx_len == 0) {
        return 0;  // 没有数据
    }

    size_t len = sockets[sockfd].rx_len;
    if (len > max_len) len = max_len;
    memcpy(buf, sockets[sockfd].rx_buf, len);

    if (src_ip) *src_ip = sockets[sockfd].remote_ip;
    if (src_port) *src_port = sockets[sockfd].remote_port;

    sockets[sockfd].rx_len = 0;  // 清空缓冲区
    return len;
}
