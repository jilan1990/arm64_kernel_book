// include/pci.h
#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stddef.h>

#define PCI_VENDOR_ID   0x00
#define PCI_DEVICE_ID   0x02
#define PCI_BAR0        0x10
#define PCI_INTERRUPT_LINE 0x3C

uint32_t pci_config_read(int bus, int dev, int func, int reg);
void pci_config_write(int bus, int dev, int func, int reg, uint32_t val);
uint32_t pci_find_device(uint16_t vendor_id, uint16_t device_id);

#endif
