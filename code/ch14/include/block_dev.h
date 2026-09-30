// include/block_dev.h
#ifndef BLOCK_DEV_H
#define BLOCK_DEV_H

#include <stddef.h>

#define MAX_BLOCK_DEVS 8
#define BLOCK_SIZE 512

struct block_dev {
    const char *name;
    int major;
    int (*open)(int minor);
    int (*close)(int minor);
    int (*read)(int minor, unsigned long block, char *buf, int count);
    int (*write)(int minor, unsigned long block, const char *buf, int count);
    unsigned long (*size)(int minor);  // 总块数
};

int block_dev_register(struct block_dev *dev);
struct block_dev *block_dev_find(int major);
int block_dev_read(int major, int minor, unsigned long block, char *buf, int count);
int block_dev_write(int major, int minor, unsigned long block, const char *buf, int count);

#endif
