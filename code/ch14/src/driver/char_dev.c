// src/driver/char_dev.c
#include "char_dev.h"
#include "uart.h"

static struct char_dev *char_devs[MAX_CHAR_DEVS];
static int char_dev_count = 0;

int char_dev_register(struct char_dev *dev) {
    if (char_dev_count >= MAX_CHAR_DEVS) return -1;
    char_devs[char_dev_count++] = dev;
    return 0;
}

struct char_dev *char_dev_find(int major) {
    for (int i = 0; i < char_dev_count; i++) {
        if (char_devs[i]->major == major) return char_devs[i];
    }
    return NULL;
}

int char_dev_open(int major, int minor) {
    struct char_dev *dev = char_dev_find(major);
    if (!dev || !dev->open) return -1;
    return dev->open(minor);
}

int char_dev_read(int major, int minor, char *buf, size_t count) {
    struct char_dev *dev = char_dev_find(major);
    if (!dev || !dev->read) return -1;
    return dev->read(minor, buf, count);
}

int char_dev_write(int major, int minor, const char *buf, size_t count) {
    struct char_dev *dev = char_dev_find(major);
    if (!dev || !dev->write) return -1;
    return dev->write(minor, buf, count);
}

// block device registration stub
#include "block_dev.h"
static struct block_dev *block_devs[8];
int block_dev_register(struct block_dev *dev) {
    for (int i = 0; i < 8; i++) {
        if (!block_devs[i]) { block_devs[i] = dev; return 0; }
    }
    return -1;
}
