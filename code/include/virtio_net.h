// include/virtio_net.h
#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include <stdint.h>
#include <stddef.h>

#define VIRTIO_NET_VENDOR_ID  0x1AF4
#define VIRTIO_NET_DEVICE_ID  0x1000
#define VIRTIO_QUEUE_SIZE     256

// virtio 寄存器偏移
#define VIRTIO_REG_DEVICE_FEATURES  0x00
#define VIRTIO_REG_GUEST_FEATURES   0x04
#define VIRTIO_REG_QUEUE_ADDR       0x08
#define VIRTIO_REG_QUEUE_SIZE       0x0C
#define VIRTIO_REG_QUEUE_SELECT     0x0E
#define VIRTIO_REG_QUEUE_NOTIFY     0x10
#define VIRTIO_REG_DEVICE_STATUS    0x12
#define VIRTIO_REG_ISR_STATUS       0x13
#define VIRTIO_REG_MAC              0x14

// 设备状态位
#define VIRTIO_STATUS_ACKNOWLEDGE   0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_DRIVER_OK     0x04
#define VIRTIO_STATUS_FEATURES_OK   0x08

// 描述符标志
#define VRING_DESC_F_NEXT   0x01
#define VRING_DESC_F_WRITE  0x02

// vring 描述符
struct vring_desc {
    uint64_t addr;      // 数据缓冲区物理地址
    uint32_t len;       // 数据长度
    uint16_t flags;     // 标志
    uint16_t next;      // 下一个描述符索引
};

// vring 可用环（设备写，驱动读）
struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

// vring 已用环（驱动写，设备读）
struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct {
        uint32_t id;
        uint32_t len;
    } ring[];
};

// virtio-net 头部
struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
};

void virtio_net_init(void);
int virtio_net_send(const uint8_t *data, size_t len);
void virtio_net_poll(void);
uint8_t *virtio_net_get_mac(void);

#endif
