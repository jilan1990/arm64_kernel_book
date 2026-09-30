// src/kernel/pci.c
// ARM64 QEMU virt PCIe ECAM 配置空间访问
// QEMU 9.0+ ECAM at 0x4010000000; older at 0x3f000000
#include "pci.h"
#include "uart.h"
#include <stdint.h>

static volatile uint32_t *ecam_base;

static inline volatile uint32_t *pci_config_addr(int bus, int dev, int func, int reg) {
    return (volatile uint32_t *)(
        (uintptr_t)ecam_base
        | ((uintptr_t)bus << 20)
        | ((uintptr_t)dev << 15)
        | ((uintptr_t)func << 12)
        | ((uintptr_t)(reg & 0xFC))
    );
}

uint32_t pci_config_read(int bus, int dev, int func, int reg) {
    return *pci_config_addr(bus, dev, func, reg);
}

void pci_config_write(int bus, int dev, int func, int reg, uint32_t val) {
    *pci_config_addr(bus, dev, func, reg) = val;
}

uint32_t pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    // QEMU virt 9.0+: PCIe ECAM at 0x4010000000
    // QEMU virt <9.0:  PCIe ECAM at 0x3f000000
    uintptr_t bases[] = { 0x4010000000ULL, 0x3f000000ULL };

    for (int b = 0; b < 2; b++) {
        ecam_base = (volatile uint32_t *)bases[b];
        uint32_t id = pci_config_read(0, 0, 0, 0);
        uint16_t vid = id & 0xFFFF;
        if (vid == 0x0000 || vid == 0xFFFF) continue;

        for (int dev = 0; dev < 32; dev++) {
            id = pci_config_read(0, dev, 0, 0);
            vid = id & 0xFFFF;
            uint16_t did = (id >> 16) & 0xFFFF;
            if (vid == 0x0000 || vid == 0xFFFF) continue;
            if (vid == vendor_id && did == device_id) {
                // Enable memory space + bus mastering
                uint32_t cmd = pci_config_read(0, dev, 0, 0x04);
                pci_config_write(0, dev, 0, 0x04, cmd | 0x6);
                // Assign BAR4 (64-bit memory BAR) to 0x10000000
                pci_config_write(0, dev, 0, 0x20, 0x10000000);
                pci_config_write(0, dev, 0, 0x24, 0);
                return 0x10000000;
            }
        }
    }
    return 0;
}
