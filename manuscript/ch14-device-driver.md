# 第十四章 设备驱动模型

## 一、概述

操作系统的一个核心功能是管理硬件设备。每种设备都有自己的操作方式，但操作系统需要为应用程序提供统一的访问接口。设备驱动模型就是解决这个问题的——它定义了一套标准的接口和数据结构，让各种设备驱动可以按照统一的方式注册和访问。


设备驱动模型的价值可以浓缩成一个问题：**上层代码怎么写，才能不认识具体设备？** 应用程序调 `read(fd, buf, n)` 时，fd 背后可能是串口、磁盘、网络卡或一个虚拟设备，应用不该关心，也不该为每种设备写一套调用方式。解决之道就是"函数指针表"：每种设备把自己的操作实现填进一个统一结构体（`open/close/read/write/...`），框架按主设备号登记在册；上层只调用框架的分发函数，由框架把调用转给对应设备。这样"设备"从具体硬件抽象成一个对象，Linux 的 `struct file_operations`、`struct block_device_operations`、设备模型（device/driver/bus）都是这一思想的生产级演化——v0.3 的字符/块设备两套框架正是它的最小投影。

本章还隐含了一个分层观：第 4 章的 PL011 裸寄存器驱动（`uart_putc`/`uart_getc`）是"最底层"，本章的 `uart_char` 把它包装成统一接口是"中间层"，第 12 章的系统调用、第 16 章的 shell 是"上层"。越往上层越不关心硬件，越往下层越贴近寄存器——读者写驱动时应始终清楚自己站在哪一层。
v0.3 实现了**字符设备层**（`char_dev`）与**块设备层**（`block_dev`）两套框架，并注册了两个设备：UART 字符设备（`ttyS0`）和 ramdisk 块设备（`ram0`）。本章代码对应最终版本 `src/driver/char_dev.c`、`include/char_dev.h`、`src/driver/uart_char.c`、`src/driver/ramdisk.c`、`include/block_dev.h`。

## 二、设备驱动的统一接口

设备驱动模型的核心思想：**定义统一的"设备对象"结构，用函数指针描述操作**，驱动只需填充这个结构并注册。

### （一） 字符设备

```c
// include/char_dev.h
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
```

三个字段各司其职：`major` 是主设备号，区分设备类别（UART = 4、ramdisk = 1），是查找设备的关键字；`minor` 是次设备号，区分同类设备的不同实例（比如两块串口卡分别是 minor 0 和 1）；`open/close/read/write/ioctl` 是一组标准操作函数指针——注意 ioctl 在 v0.3 的 UART 上为 NULL，框架分发前必须判空，这是"接口可以留空"的设计：驱动只填自己支持的函数，不支持的置 NULL 交给框架层防御。
### （二） 块设备

```c
// include/block_dev.h
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
```

块设备以**块**（512 字节）为单位读写，与字符设备的字节流模型不同。

## 三、设备注册与查找

`char_dev.c` 维护设备表：

```c
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
```

注册只是把设备对象放进数组，查找按主设备号线性扫描。块设备层同样有 `block_dev_register`——它在 v0.3 中作为**简单 stub** 位于 `char_dev.c` 文件末尾（真源即如此，块设备框架只做到能注册，供 ramdisk 使用）：

```c
// block device registration stub
#include "block_dev.h"
static struct block_dev *block_devs[8];
int block_dev_register(struct block_dev *dev) {
    for (int i = 0; i < 8; i++) {
        if (!block_devs[i]) { block_devs[i] = dev; return 0; }
    }
    return -1;
}
```

## 四、UART 字符设备

`uart_char.c` 把底层 PL011 驱动（第 4 章）包装成标准字符设备：

```c
// src/driver/uart_char.c
#include "char_dev.h"
#include "uart.h"

#define UART_MAJOR 4

static int uart_read(int minor, char *buf, size_t count) {
    (void)minor;
    size_t i = 0;
    while (i < count) {
        char c = uart_getc();
        buf[i++] = c;
        if (c == '\n') break;
    }
    return i;
}

static int uart_write(int minor, const char *buf, size_t count) {
    (void)minor;
    for (size_t i = 0; i < count; i++) {
        uart_putc(buf[i]);
    }
    return count;
}

static struct char_dev uart_dev = {
    .name = "ttyS0",
    .major = UART_MAJOR,
    .open = uart_open,
    .close = uart_close,
    .read = uart_read,
    .write = uart_write,
    .ioctl = NULL,
};

void uart_char_init(void) {
    char_dev_register(&uart_dev);
}
```

要点集中在三个设计选择。其一，`uart_read` 读到 `\n` 就停，这是行读取语义——控制台输入天然以行为单位，`console_readline`（第 12 章）与它配合，上层拿到一整行而不是字节流。其二，`uart_write` 逐字节调用 `uart_putc`，每一字节都走 PL011 的轮询发送（第 4 章），简单且无缓冲竞争。其三，未实现的接口置 NULL 而不是空函数——框架层在分发函数里统一判空（`if (!dev || !dev->read) return -1`），让"驱动漏填接口"显式报错而不是静默失败，这是接口留空的安全策略。
分发函数示例：

```c
int char_dev_read(int major, int minor, char *buf, size_t count) {
    struct char_dev *dev = char_dev_find(major);
    if (!dev || !dev->read) return -1;
    return dev->read(minor, buf, count);
}
```

## 五、ramdisk 块设备

ramdisk 用内存模拟磁盘——一块 1MB 的内存区域当作块设备：

```c
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
```

设计点有三个。数据区用 `kmalloc` 一次性分配 2048×512 = 1MB——超过 4096 上限走第 9 章的大对象直通页分配路径，`ramdisk_open` 里惰性分配（首次 open 才分配），避免空转内存。`ramdisk_read/write` 的偏移计算是 `offset = block * BLOCK_SIZE`，先做 `block + count > RAMDISK_BLOCKS` 的越界检查再拷贝——设备驱动最重要的正确性边界就是"参数越界必须被挡住"。读写本身是纯内存拷贝，没有任何真实硬件延迟，这正是"用内存模拟磁盘"的代价与收益：测试文件系统时不依赖磁盘，但也无法暴露真实磁盘的延迟问题。
## 六、设备初始化

`proc.c` 中的 `device_init` 注册所有设备：

```c
void device_init(void) {
    extern void ramdisk_init(void);
    extern void uart_char_init(void);
    ramdisk_init();
    uart_char_init();
}
```

启动顺序（main.c）：`device_init()` 在 `vfs_init()` 之前调用，文件系统需要块设备时设备已就绪。

## 七、设备驱动模型的价值

没有框架也能写驱动（直接 `uart_puts`），那为什么还要分层？

首先是**统一接口**：上层代码（VFS、系统调用、未来的文件系统挂载）只依赖 `char_dev`/`block_dev` 接口，不认识具体设备——这就是概述里"上层不关心硬件"的落地。其次是**可替换性**：将来把 `ram0` 换成真实 virtio-blk 驱动，只需新增一个 `block_dev` 结构并注册，上层零改动——驱动开发的核心流程"写实现 → 填结构 → 注册"被固定下来。再次是**可测试性**：ramdisk 让第 15 章的文件系统在无真实磁盘的情况下完整可测，块设备接口成为"桩（stub）"的标准范例。最后是**向 Linux 靠拢**：Linux 的设备模型正是同一思路的完整版，`struct file_operations` 对应 `char_dev`、`struct block_device_operations` 对应 `block_dev`，但还多了设备号申请、总线匹配、电源管理等大量工程细节——理解了本章的 60 行，再看 Linux 驱动源码的 `probe` 回调与 ops 表就不会陌生。
## 八、小结

本章实现了两层设备框架与两个注册设备。字符设备框架：`char_dev` 结构以 open/close/read/write/ioctl 函数指针表描述设备，`char_dev_register` 登记、`char_dev_find` 按主设备号查找、分发函数判空转发。块设备框架：`block_dev` 以块为单位读写并支持容量查询，`block_dev_register` stub 位于 char_dev.c 末尾。注册的两个设备中，UART 字符设备 `ttyS0`（主设备号 4）提供行读取与字节输出，ramdisk 块设备 `ram0`（主设备号 1）用 1MB 内存模拟 2048 个 512 字节块。整章贯穿"函数指针表 + 注册 + 分发"的驱动范式，为第 15 章文件系统、第 18 章 virtio 网络驱动提供了统一的设备抽象。
## 九、练习

练习一，给 ramdisk 增加一个 `flush` 接口（打印日志并返回 0），在 `block_dev` 结构里加一个函数指针，然后在某次写入后显式调用，观察调用时机——这会让你亲手改一遍"结构体加字段 → 驱动实现 → 上层调用"的完整链路。练习二，新增一个虚拟字符设备 `/dev/null`（`read` 返回 0、`write` 丢弃数据返回 count），仿照 `uart_char_init` 写一个 `null_dev_init` 注册它，体会"设备不一定要有硬件"的抽象力量。练习三，把 `char_dev_find` 改成按 major 哈希查找（major 范围小，可以直接用数组下标），对比线性扫描与 O(1) 查找的实现差异——16 项设备表里差别不大，但理解"查找结构跟着规模走"的选择逻辑。
