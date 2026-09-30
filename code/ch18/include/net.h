// include/net.h
#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>

// 网络配置（QEMU 用户模式网络）
#define NET_IP_ADDR      0x0A00020F  // 10.0.2.15
#define NET_NETMASK      0xFFFFFF00  // 255.255.255.0
#define NET_GATEWAY      0x0A000202  // 10.0.2.2
#define NET_DNS          0x0A000203  // 10.0.2.3

// 以太网帧类型
#define ETH_TYPE_IPV4    0x0800
#define ETH_TYPE_ARP     0x0806

// IP 协议号
#define IP_PROTO_ICMP    1
#define IP_PROTO_TCP     6
#define IP_PROTO_UDP     17

// 以太网地址长度
#define ETH_ALEN         6
// IP 地址长度
#define IP_ALEN          4

// 以太网头部
struct ethhdr {
    uint8_t  h_dest[ETH_ALEN];   // 目的 MAC
    uint8_t  h_source[ETH_ALEN]; // 源 MAC
    uint16_t h_proto;             // 类型（大端）
};

// ARP 头部
// 注意：必须紧凑布局，报文不允许有填充字节
struct __attribute__((packed)) arphdr {
    uint16_t ar_hrd;    // 硬件类型
    uint16_t ar_pro;    // 协议类型
    uint8_t  ar_hln;    // 硬件地址长度
    uint8_t  ar_pln;    // 协议地址长度
    uint16_t ar_op;     // 操作码
    uint8_t  ar_sha[ETH_ALEN];  // 发送端 MAC
    uint32_t ar_sip;             // 发送端 IP
    uint8_t  ar_tha[ETH_ALEN];  // 目标端 MAC
    uint32_t ar_tip;             // 目标端 IP
};

#define ARP_OP_REQUEST   1
#define ARP_OP_REPLY     2

// IP 头部（不含选项）
struct iphdr {
    uint8_t  ihl_version;   // 版本(高4位) + IHL(低4位)
    uint8_t  tos;           // 服务类型
    uint16_t tot_len;       // 总长度
    uint16_t id;            // 标识
    uint16_t frag_off;      // 标志(高3位) + 片偏移(低13位)
    uint8_t  ttl;           // 生存时间
    uint8_t  protocol;      // 协议
    uint16_t check;         // 头部校验和
    uint32_t saddr;         // 源 IP
    uint32_t daddr;         // 目的 IP
};

// ICMP 头部
struct icmphdr {
    uint8_t  type;      // 类型
    uint8_t  code;      // 代码
    uint16_t checksum;  // 校验和
    uint16_t id;        // 标识符
    uint16_t sequence;  // 序列号
};

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

// UDP 头部
struct udphdr {
    uint16_t source;   // 源端口
    uint16_t dest;     // 目的端口
    uint16_t len;      // 长度
    uint16_t check;    // 校验和
};

void net_init(void);
void net_rx_handler(uint8_t *data, size_t len);
uint16_t ip_checksum(const void *data, size_t len);
uint32_t resolve_arp(uint32_t ip);

#endif
