// include/char_dev.h
#ifndef CHAR_DEV_H
#define CHAR_DEV_H

#include <stddef.h>

#define MAX_CHAR_DEVS 16

struct char_dev {
    const char *name;
    int major;
    int (*open)(int minor);
    int (*close)(int minor);
    int (*read)(int minor, char *buf, size_t count);
    int (*write)(int minor, const char *buf, size_t count);
    int (*ioctl)(int minor, int cmd, void *arg);
};

int char_dev_register(struct char_dev *dev);
struct char_dev *char_dev_find(int major);
int char_dev_open(int major, int minor);
int char_dev_close(int major, int minor);
int char_dev_read(int major, int minor, char *buf, size_t count);
int char_dev_write(int major, int minor, const char *buf, size_t count);

#endif
