// src/kernel/virtio_net.c
// virtio-net PCI 驱动（QEMU virt, modern virtio PCI layout in BAR4）
#include "virtio_net.h"
#include "pci.h"
#include "uart.h"
#include "string.h"
#include "net.h"

static volatile uint8_t *io_base;
static uint8_t mac_addr[6];
static uint32_t notify_off_multiplier;
static uintptr_t notify_base;
static uintptr_t device_cfg_base;

// vring 布局：desc 表 + avail 环放在同一块 4K 对齐内存，
// used 环单独放一块 4K 对齐内存（VirtIO 规范要求 used ring 页对齐）
static uint8_t rx_queue_mem[VIRTIO_QUEUE_SIZE * 16 + 6 + VIRTIO_QUEUE_SIZE * 2] __attribute__((aligned(4096)));
static uint8_t tx_queue_mem[VIRTIO_QUEUE_SIZE * 16 + 6 + VIRTIO_QUEUE_SIZE * 2] __attribute__((aligned(4096)));
static uint8_t rx_used_mem[VIRTIO_QUEUE_SIZE * 8 + 8] __attribute__((aligned(4096)));
static uint8_t tx_used_mem[VIRTIO_QUEUE_SIZE * 8 + 8] __attribute__((aligned(4096)));

static uint8_t rx_buffers[VIRTIO_QUEUE_SIZE][1526];
static uint8_t tx_buffers[VIRTIO_QUEUE_SIZE][1526];

// CommonCfg register offsets (BAR offset 0)
#define DEVICE_FEATURES_SEL  0x00
#define DEVICE_FEATURES      0x04
#define DRIVER_FEATURES_SEL  0x08
#define DRIVER_FEATURES      0x0C
#define DEVICE_STATUS        0x14
#define QUEUE_SEL            0x16
#define QUEUE_SIZE           0x18
#define QUEUE_ENABLE         0x1C
#define QUEUE_NOTIFY_OFF     0x1E
#define QUEUE_DESC_LO        0x20
#define QUEUE_DESC_HI        0x24
#define QUEUE_DRIVER_LO      0x28
#define QUEUE_DRIVER_HI      0x2C
#define QUEUE_DEVICE_LO      0x30
#define QUEUE_DEVICE_HI      0x34

static inline void write8(uintptr_t off, uint8_t val) {
    *(volatile uint8_t *)(io_base + off) = val;
}
static inline uint8_t read8(uintptr_t off) {
    return *(volatile uint8_t *)(io_base + off);
}
static inline void write16(uintptr_t off, uint16_t val) {
    *(volatile uint16_t *)(io_base + off) = val;
}
static inline uint16_t read16(uintptr_t off) {
    return *(volatile uint16_t *)(io_base + off);
}
static inline void write32(uintptr_t off, uint32_t val) {
    *(volatile uint32_t *)(io_base + off) = val;
}

void virtio_net_init(void) {
    uint32_t bar = pci_find_device(0x1af4, 0x1000);
    if (!bar) {
        uart_puts("virtio-net: device not found\n");
        return;
    }
    io_base = (volatile uint8_t *)(uintptr_t)bar;

    // Parse PCI capabilities to find region offsets
    int cap_ptr = pci_config_read(0, 1, 0, 0x34) & 0xFF;
    notify_base = 0x3000;
    device_cfg_base = 0x2000;
    notify_off_multiplier = 4;

    while (cap_ptr) {
        uint32_t w0 = pci_config_read(0, 1, 0, cap_ptr);
        uint8_t cap_id = w0 & 0xFF;
        uint8_t next_ptr = (w0 >> 8) & 0xFF;
        uint8_t cfg_type = (w0 >> 24) & 0xFF;

        if (cap_id == 0x09) {
            uint32_t w2 = pci_config_read(0, 1, 0, cap_ptr + 8);
            if (cfg_type == 2) {  // Notify
                notify_base = w2;
                notify_off_multiplier = pci_config_read(0, 1, 0, cap_ptr + 16);
            } else if (cfg_type == 4) {  // Device config
                device_cfg_base = w2;
            }
        }
        cap_ptr = next_ptr;
    }

    // Read MAC from device config
    for (int i = 0; i < 6; i++)
        mac_addr[i] = read8(device_cfg_base + i);

    // Reset and negotiate features
    write8(DEVICE_STATUS, 0);
    write8(DEVICE_STATUS, 1);                    // ACKNOWLEDGE
    write8(DEVICE_STATUS, 1 | 2);                // DRIVER
    write32(DRIVER_FEATURES_SEL, 0);
    write32(DRIVER_FEATURES, 0);
    write8(DEVICE_STATUS, 1 | 2 | 8);            // FEATURES_OK

    if (!(read8(DEVICE_STATUS) & 8)) {
        uart_puts("virtio-net: features failed\n");
        return;
    }

    // Set up queue 0 (RX) and queue 1 (TX)
    for (int q = 0; q < 2; q++) {
        uint16_t qsize;
        uintptr_t qmem;
        write16(QUEUE_SEL, q);
        if (q == 0) { qsize = read16(QUEUE_SIZE); qmem = (uintptr_t)rx_queue_mem; }
        else        { write16(QUEUE_SEL, 1); qsize = read16(QUEUE_SIZE); qmem = (uintptr_t)tx_queue_mem; }

        uint64_t desc_pa = qmem;
        write32(QUEUE_DESC_LO, (uint32_t)desc_pa);
        write32(QUEUE_DESC_HI, (uint32_t)(desc_pa >> 32));
        uint64_t avail_pa = desc_pa + qsize * 16;
        write32(QUEUE_DRIVER_LO, (uint32_t)avail_pa);
        write32(QUEUE_DRIVER_HI, (uint32_t)(avail_pa >> 32));
        uint64_t used_pa = (uintptr_t)((q == 0) ? rx_used_mem : tx_used_mem);
        write32(QUEUE_DEVICE_LO, (uint32_t)used_pa);
        write32(QUEUE_DEVICE_HI, (uint32_t)(used_pa >> 32));
        write16(QUEUE_ENABLE, 1);
    }

    write8(DEVICE_STATUS, 1 | 2 | 8 | 4);  // DRIVER_OK

    // Fill RX queue
    write16(QUEUE_SEL, 0);
    uint16_t qsize = read16(QUEUE_SIZE);
    struct vring_desc *desc = (struct vring_desc *)rx_queue_mem;
    for (int i = 0; i < qsize; i++) {
        // QEMU DMA 写窗口为 [addr+10, addr+len)，补偿使数据落在缓冲区起点
        desc[i].addr = (uint64_t)(uintptr_t)rx_buffers[i] - 10;
        desc[i].len = 1526;
        desc[i].flags = VRING_DESC_F_WRITE;
        desc[i].next = 0;
    }
    struct vring_avail *avail = (struct vring_avail *)(rx_queue_mem + qsize * 16);
    avail->flags = 0;
    avail->idx = qsize;
    for (int i = 0; i < qsize; i++)
        avail->ring[i] = i;

    // Notify queue 0
    uint16_t noff = read16(QUEUE_NOTIFY_OFF);
    write16(notify_base + (uint32_t)noff * notify_off_multiplier, 0);

    uart_puts("virtio-net: initialized, MAC ");
    for (int i = 0; i < 6; i++) {
        if (i) uart_putc(':');
        uart_puthex_byte(mac_addr[i]);
    }
    uart_puts("\n");
}

uint8_t *virtio_net_get_mac(void) {
    return mac_addr;
}

int virtio_net_send(const uint8_t *data, size_t len) {
    static uint16_t tx_idx = 0;
    uint16_t qsize = VIRTIO_QUEUE_SIZE;
    struct vring_desc *desc = (struct vring_desc *)tx_queue_mem;
    uint16_t idx = tx_idx % qsize;
    // 拷贝到静态缓冲：设备 DMA 读期间数据必须保持有效
    for (size_t i = 0; i < len; i++) tx_buffers[idx][i] = data[i];
    // QEMU DMA 读窗口为 [addr+10, addr+len)，用 addr-10 / len+10 补偿
    desc[idx].addr = (uint64_t)(uintptr_t)tx_buffers[idx] - 10;
    desc[idx].len = (uint32_t)len + 10;
    desc[idx].flags = 0;
    desc[idx].next = 0;

    struct vring_avail *avail = (struct vring_avail *)(tx_queue_mem + qsize * 16);
    avail->ring[tx_idx % qsize] = idx;
    avail->idx = tx_idx + 1;
    tx_idx++;

    write16(QUEUE_SEL, 1);
    uint16_t noff = read16(QUEUE_NOTIFY_OFF);
    write16(notify_base + (uint32_t)noff * notify_off_multiplier, 1);

    // 同步等待设备完成发送（DMA 读完数据缓冲区后才可被调用方释放）
    struct vring_used *tx_used = (struct vring_used *)tx_used_mem;
    while (tx_used->idx != (uint16_t)tx_idx) ;

    return len;
}

// 轮询 RX 队列：把收到的包交给协议栈（非阻塞，无数据时立即返回）
void virtio_net_poll(void) {
    uint16_t qsize = VIRTIO_QUEUE_SIZE;
    struct vring_used *used = (struct vring_used *)rx_used_mem;
    struct vring_avail *avail = (struct vring_avail *)(rx_queue_mem + qsize * 16);
    static uint16_t last_used = 0;

    while (last_used != used->idx) {
        uint16_t u = last_used % qsize;
        uint32_t id = used->ring[u].id;
        uint32_t len = used->ring[u].len;
        last_used++;

        uint8_t *pkt = rx_buffers[id];
        net_rx_handler(pkt, len);

        // 把缓冲区归还 RX 队列
        uint16_t a = avail->idx;
        avail->ring[a % qsize] = id;
        avail->idx = a + 1;
        write16(QUEUE_SEL, 0);
        uint16_t noff = read16(QUEUE_NOTIFY_OFF);
        write16(notify_base + (uint32_t)noff * notify_off_multiplier, 0);
    }
}
