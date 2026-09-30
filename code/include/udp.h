// include/udp.h
#ifndef UDP_H
#define UDP_H

#include "net.h"

#define UDP_MAX_SOCKETS  16
#define UDP_RX_BUF_SIZE  4096

struct udp_socket {
    int      used;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;

    uint8_t  rx_buf[UDP_RX_BUF_SIZE];
    size_t   rx_len;
};

void udp_init(void);
void udp_rx(struct iphdr *ip, uint8_t *data, size_t len);
int udp_socket(void);
int udp_bind(int sockfd, uint16_t port);
int udp_sendto(int sockfd, const uint8_t *data, size_t len, uint32_t dst_ip, uint16_t dst_port);
int udp_recvfrom(int sockfd, uint8_t *buf, size_t max_len, uint32_t *src_ip, uint16_t *src_port);

#endif
