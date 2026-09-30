// src/driver/ramdisk.c
#include "block_dev.h"
#include "mm.h"
#include "slab.h"
#include "uart.h"

#define RAMDISK_MAJOR 1
#define RAMDISK_BLOCKS 2048  // 2048 * 512 = 1MB

static char *ramdisk_data = NULL;

static int ramdisk_open(int minor) {
    (void)minor;
    if (!ramdisk_data) {
        ramdisk_data = kmalloc(RAMDISK_BLOCKS * BLOCK_SIZE);
        if (!ramdisk_data) return -1;
    }
    return 0;
}

static int ramdisk_read(int minor, unsigned long block, char *buf, int count) {
    (void)minor;
    if (block + count > RAMDISK_BLOCKS) return -1;
    unsigned long offset = block * BLOCK_SIZE;
    for (int i = 0; i < count * BLOCK_SIZE; i++) {
        buf[i] = ramdisk_data[offset + i];
    }
    return count;
}

static int ramdisk_write(int minor, unsigned long block, const char *buf, int count) {
    (void)minor;
    if (block + count > RAMDISK_BLOCKS) return -1;
    unsigned long offset = block * BLOCK_SIZE;
    for (int i = 0; i < count * BLOCK_SIZE; i++) {
        ramdisk_data[offset + i] = buf[i];
    }
    return count;
}

static unsigned long ramdisk_size(int minor) {
    (void)minor;
    return RAMDISK_BLOCKS;
}

static struct block_dev ramdisk_dev = {
    .name = "ram0",
    .major = RAMDISK_MAJOR,
    .open = ramdisk_open,
    .close = NULL,
    .read = ramdisk_read,
    .write = ramdisk_write,
    .size = ramdisk_size,
};

void ramdisk_init(void) {
    block_dev_register(&ramdisk_dev);
}
