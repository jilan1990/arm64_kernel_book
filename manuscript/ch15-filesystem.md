# 第十五章 文件系统

## 一、概述

文件系统是操作系统中负责组织和管理数据的子系统。它把存储设备上的原始字节组织成文件和目录，提供创建、删除、读写、查找等操作。


先理解"文件系统"这个词的层次。最底层是存储介质（磁盘、内存、NAND），只提供"按块读写"；文件系统在这一层之上建立**命名空间**——把字节组织成带名字的文件、把文件组织进目录树，并用 inode 记录每个文件的元数据（大小、类型、创建者等）。用户接触到的"文件"，本质是"名字 → inode → 数据块"三级映射。Linux 在这个基础上再叠一层 **VFS（虚拟文件系统）**：它定义一套统一接口（inode/file/dentry/超级块），让 ext4、tmpfs、procfs、NFS 等差异巨大的文件系统都能被 `open()` 同一套系统调用使用——`/proc` 是内存、`/mnt/nfs` 是网络、`/` 是磁盘，但对应用来说都是文件。

v0.3 的 tmpfs 正好落在"文件系统本体"与"VFS 抽象"的交界：`tmpfs.c` 既实现了 inode 树本体，又提供了一组 `vfs_*` 公共接口。之所以选择内存文件系统而不是磁盘文件系统，有三个现实理由：不需要处理块设备 I/O 的时序与缓冲（第 18 章才有 virtio 网络，块设备只有 stub）、不依赖真实磁盘就能演示文件语义、数据在内存里天然可调试（GDB 直接看 inode 结构）。理解 tmpfs，就理解了所有文件系统的共性骨架；差异只是"数据最终落在哪、怎么索引"。
v0.3 实现了一个**内存文件系统（tmpfs）**：文件和目录都是内存中的 inode 节点，不涉及真实磁盘。它足够支撑 shell 的 `cat`、`ls`、`mkdir` 命令，也展示了 Linux VFS（虚拟文件系统）的核心思想。本章代码对应最终版本 `src/fs/tmpfs.c`、`include/vfs.h`。

## 二、数据结构：inode 树

tmpfs 的核心是 inode 树：目录是 inode，文件也是 inode，目录通过 `children` 链表挂子节点。v0.3 的接口定义：

```c
// include/vfs.h
#ifndef VFS_H
#define VFS_H

#include <stddef.h>

#define MAX_FILES 64
#define MAX_PATH 256
#define FS_NAME_LEN 32

// open 标志
#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_RDWR   0x002
#define O_CREAT  0x100

// 文件类型
enum file_type {
    FS_TYPE_FILE = 1,
    FS_TYPE_DIR = 2,
};

// inode：表示一个文件
struct inode {
    int ino;
    enum file_type type;
    size_t size;
    void *data;
    struct inode *parent;
    struct inode *children;
    struct inode *next;
    char name[FS_NAME_LEN];
    int refcount;
};

// file：表示一个打开的文件
struct file {
    struct inode *inode;
    size_t offset;
    int flags;
    int used;
};

// 文件系统操作接口
struct fs_operations {
    struct inode *(*lookup)(struct inode *dir, const char *name);
    struct inode *(*create)(struct inode *dir, const char *name, enum file_type type);
    int (*unlink)(struct inode *dir, const char *name);
    int (*read)(struct file *fp, char *buf, size_t count);
    int (*write)(struct file *fp, const char *buf, size_t count);
};

void vfs_init(void);
struct file *vfs_open(const char *path, int flags);
int vfs_close(struct file *fp);
int vfs_read(struct file *fp, char *buf, size_t count);
int vfs_write(struct file *fp, const char *buf, size_t count);
int vfs_mkdir(const char *path);
int vfs_list(const char *path);
struct inode *vfs_path_lookup(const char *path);

#endif
```

设计特点围绕三个结构展开。其一，`struct file` 是静态数组元素：`tmpfs.c` 维护 `static struct file file_table[MAX_FILES]`，打开文件就是占一个 `used = 1` 的槽位，返回 `&file_table[i]`——`sys_open` 把这个指针直接当 fd 传给用户程序（第 12 章的"指针即 fd"模型），关闭就是把 `used` 清 0。这个设计把"打开文件表"简化为固定数组，代价是同时打开的文件数上限 64。其二，`struct fs_operations` 是文件系统方法表：`lookup/create/unlink/read/write` 五个函数指针，VFS 层只调用方法表不关心实现，为将来挂载第二个文件系统（如 ext2）预留了接口——这正是 Linux VFS 的 `super_operations`/`inode_operations`/`file_operations` 的最小形态。其三，inode 字段各司其职：`ino` 是文件系统内唯一编号，`type` 区分文件与目录，`size` 与 `data` 描述文件内容，`parent/children/next` 把 inode 组织成树与兄弟链表，`refcount` 记录引用计数（当前恒为 1，为将来删除语义预留）。
## 三、tmpfs 实现

### （一） inode 分配

```c
// src/fs/tmpfs.c
// 内存文件系统 + VFS 公共接口（open/close/read/write/mkdir/list/lookup）
#include "vfs.h"
#include "slab.h"
#include "uart.h"
#include "string.h"

static struct inode *root_inode = NULL;
static struct file file_table[MAX_FILES];
static int next_ino = 1;

static struct inode *tmpfs_lookup(struct inode *dir, const char *name);
static struct inode *tmpfs_create(struct inode *dir, const char *name, enum file_type type);
static int tmpfs_read(struct file *fp, char *buf, size_t count);
static int tmpfs_write(struct file *fp, const char *buf, size_t count);

static struct inode *alloc_inode(const char *name, enum file_type type, struct inode *parent) {
    struct inode *ino = kmalloc(sizeof(struct inode));
    if (!ino) return NULL;

    ino->ino = next_ino++;
    ino->type = type;
    ino->size = 0;
    ino->data = NULL;
    ino->parent = parent;
    ino->children = NULL;
    ino->next = NULL;
    ino->refcount = 1;

    strncpy(ino->name, name, FS_NAME_LEN - 1);
    ino->name[FS_NAME_LEN - 1] = '\0';
    return ino;
}
```

inode 用 `kmalloc`（第 9 章 slab）分配。`name` 固定 32 字节，拷贝时截断到 `FS_NAME_LEN - 1` 并补 `\0`。


`alloc_inode` 是全书的"对象生命周期"样板：`kmalloc` 分配 → 逐字段初始化 → 挂进父目录的 children 链表。注意 `next_ino` 是全局递增计数器，保证文件系统内 inode 编号唯一；`refcount` 初值 1（当前引用者就是创建者），将来实现删除时，只有 refcount 归零才能真正释放 inode——这是所有文件系统"延迟删除"的基础（Linux 的 inode 还有 `i_count` 与 `i_nlink` 双重计数：nlink 为 0 且无引用时才销毁）。`name` 用定长数组 32 字节而不是指针，拷贝时 `strncpy` 截断到 31 字节并补 `\0`——定长数组让 inode 大小固定、分配简单，代价是文件名最长 31 字节。真实文件系统（ext4）的目录项也是定长（255 字节）加截断语义，思路一致。
### （二） 查找与创建

```c
static struct inode *tmpfs_lookup(struct inode *dir, const char *name) {
    if (!dir || dir->type != FS_TYPE_DIR) return NULL;

    for (struct inode *child = dir->children; child; child = child->next) {
        if (strcmp(child->name, name) == 0)
            return child;
    }
    return NULL;
}

static struct inode *tmpfs_create(struct inode *dir, const char *name, enum file_type type) {
    if (!dir || dir->type != FS_TYPE_DIR) return NULL;
    if (tmpfs_lookup(dir, name)) return NULL;   // 重名拒绝

    struct inode *ino = alloc_inode(name, type, dir);
    if (!ino) return NULL;

    ino->next = dir->children;
    dir->children = ino;
    return ino;
}
```

查找是线性扫描 `children` 链表；创建先查重名再头插。


`tmpfs_lookup` 先检查 `dir` 有效且是目录，再沿 `children` 链表逐个 `strcmp`——O(n) 的目录查找，n 是目录项数。真实文件系统把目录项组织成哈希表或 B 树（ext4 用 htree）把查找压到对数或常数时间，tmpfs 的线性扫描在几十个文件的规模下足够。`tmpfs_create` 的"先查重名再头插"保证目录内名字唯一——这是文件系统最重要的不变量之一，重名创建在 Linux 返回 `EEXIST`，v0.3 返回 NULL。头插（`ino->next = dir->children; dir->children = ino;`）是 O(1) 的链表插入，新文件出现在 `vfs_list` 输出的最前面。
### （三） 文件读写

```c
static int tmpfs_read(struct file *fp, char *buf, size_t count) {
    struct inode *ino = fp->inode;
    if (!ino || ino->type != FS_TYPE_FILE) return -1;

    if (fp->offset >= ino->size) return 0;   // EOF

    size_t available = ino->size - fp->offset;
    if (count > available) count = available;

    memcpy(buf, (char *)ino->data + fp->offset, count);
    fp->offset += count;
    return (int)count;
}

static int tmpfs_write(struct file *fp, const char *buf, size_t count) {
    struct inode *ino = fp->inode;
    if (!ino || ino->type != FS_TYPE_FILE) return -1;

    size_t new_size = fp->offset + count;
    if (new_size > ino->size) {
        void *new_data = kmalloc(new_size);
        if (!new_data) return -1;
        if (ino->data) {
            memcpy(new_data, ino->data, ino->size);
            kfree(ino->data);
        }
        ino->data = new_data;
        ino->size = new_size;
    }

    memcpy((char *)ino->data + fp->offset, buf, count);
    fp->offset += count;
    return (int)count;
}
```

写路径的关键：**按需扩容**。写入超过当前文件大小时，`kmalloc` 一块更大的缓冲，把旧数据拷过去、释放旧缓冲，然后写新数据。读路径超 EOF 时截断到文件剩余大小。


tmpfs 的读写都围绕 `fp->offset`（当前文件偏移）展开。`tmpfs_read` 的三步：偏移已超文件尾返回 0（EOF 语义，对应 Linux 的 `read` 返回 0 表示文件结束）；否则计算可读字节数 `ino->size - fp->offset`，`count` 超出时截断；`memcpy` 数据并推进偏移。`tmpfs_write` 的核心是**按需扩容**：`new_size = fp->offset + count` 超过当前大小时，`kmalloc` 新缓冲、拷贝旧数据、`kfree` 旧缓冲、更新 `size`——这是"追加写文件会变长"的直接实现。注意扩容是整体搬移（不是原地增长），写大文件时每次扩容都复制全量数据，最坏 O(n²)；真实文件系统用块分配（ext4 的 extent 树）避免复制。两处都对 `fp->inode` 的类型做了检查（必须是文件不是目录），这是"目录不可读写"的文件系统不变量。
### （四） 文件系统方法表

```c
static struct fs_operations tmpfs_ops = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .unlink = NULL,
    .read = tmpfs_read,
    .write = tmpfs_write,
};
```

`unlink` 暂未实现（置 NULL），VFS 层调用前判空。

## 四、VFS 公共接口

### （一） 路径解析

```c
// 按路径查找：从根开始逐级 lookup
struct inode *vfs_path_lookup(const char *path) {
    if (!path || path[0] != '/') return NULL;

    char buf[MAX_PATH];
    strncpy(buf, path, MAX_PATH - 1);
    buf[MAX_PATH - 1] = '\0';

    struct inode *cur = root_inode;
    char *p = buf + 1;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        char *start = p;
        while (*p && *p != '/') p++;
        char saved = *p;
        *p = '\0';

        cur = tmpfs_ops.lookup(cur, start);
        if (!cur) return NULL;

        *p = saved;
    }
    return cur;
}
```

逐级下降：按 `/` 分隔每一级名字，调用方法表的 `lookup`。这是 VFS 层调用文件系统方法表的典型例子。


`vfs_path_lookup` 是全书对字符串处理最集中的函数，值得逐行走：先复制路径到本地缓冲（不能直接改用户的 `path` 指针，那可能是只读内存），要求以 `/` 开头（绝对路径语义）；从 `buf + 1` 开始（跳过开头的 `/`），外层循环处理每一级：跳过连续 `/`（容忍 `/a//b`），定位到组件起点，扫到下一个 `/` 或结尾，把分隔符临时改成 `\0` 得到这一级名字，调用 `tmpfs_ops.lookup(cur, start)` 下降一层，再把 `\0` 还原成 `/`（`saved` 变量保存原字符）。**在副本上做破坏性修改并还原**是这个函数最巧妙的点：不需要为每级分配子串。逐级下降失败（某级不存在）立即返回 NULL。

`split_path` 则承担相反的工作：把 `"/a/b/c"` 拆成父路径 `"/a/b"` 与末级名字 `"c"`。先去掉尾部多余 `/`（`/a/b/` 归一为 `/a/b`），从右往左找最后一个 `/`；找不到或它就是根，则父路径是 `/`、名字是去掉前导 `/` 的剩余部分；否则把该 `/` 改 `\0` 拆分。这两个函数合起来构成 tmpfs 的全部路径操作基础，`vfs_open`/`vfs_mkdir` 都建立在它们之上。
```c
// 把路径拆成父目录路径和最后一级名字
static void split_path(char *path, char **parent_path, char **name) {
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') path[--len] = '\0';

    char *slash = NULL;
    for (size_t i = 0; path[i]; i++)
        if (path[i] == '/') slash = &path[i];

    if (!slash || slash == path) {
        *parent_path = "/";
        *name = slash ? slash + 1 : path;
    } else {
        *slash = '\0';
        *parent_path = path;
        *name = slash + 1;
    }
}
```

### （二） 打开与关闭

```c
struct file *vfs_open(const char *path, int flags) {
    if (!path || path[0] != '/') return NULL;

    struct inode *ino = vfs_path_lookup(path);

    if (!ino && (flags & O_CREAT)) {
        char pbuf[MAX_PATH];
        strncpy(pbuf, path, MAX_PATH - 1);
        pbuf[MAX_PATH - 1] = '\0';

        char *parent_path, *name;
        split_path(pbuf, &parent_path, &name);

        struct inode *parent = vfs_path_lookup(parent_path);
        if (!parent || parent->type != FS_TYPE_DIR || !name[0]) return NULL;
        ino = tmpfs_create(parent, name, FS_TYPE_FILE);
    }

    if (!ino || ino->type != FS_TYPE_FILE) return NULL;

    for (int i = 0; i < MAX_FILES; i++) {
        if (!file_table[i].used) {
            file_table[i].used = 1;
            file_table[i].inode = ino;
            file_table[i].flags = flags;
            file_table[i].offset = 0;
            return &file_table[i];
        }
    }
    return NULL;   // 文件表满
}

int vfs_close(struct file *fp) {
    if (!fp || !fp->used) return -1;
    fp->used = 0;
    fp->inode = NULL;
    return 0;
}
```

`O_CREAT` 语义：路径不存在时，拆出父目录和名字，在父目录下创建文件（重复创建返回 NULL）。关闭只是释放文件表槽位。


`vfs_open` 的流程体现了 open 的两阶段语义：先 `vfs_path_lookup` 找路径；找不到且带 `O_CREAT` 时走创建路径——复制路径、`split_path` 拆父目录与名字、父目录必须存在且是目录、名字非空，然后 `tmpfs_create(parent, name, FS_TYPE_FILE)`。创建成功后继续走"找到"分支：类型必须为文件（`open` 一个目录在 Linux 返回 `EISDIR`，v0.3 返回 NULL），然后在 `file_table` 里找空槽位（`used == 0`），填入 inode/flags/offset=0，返回 `&file_table[i]`。注意 `flags` 只被记录、不被执行——v0.3 不区分只读/只写权限，这是教学简化；真实内核的 `vfs_open` 还要校验权限、设置 `FMODE_READ/WRITE`、触发 `->open` 回调。`vfs_close` 只是清 `used` 与 inode 指针，不释放 inode 本身——tmpfs 的 inode 生命周期由目录树管理，与"打开"无关（这也是为什么删除语义需要 refcount 的原因）。
### （三） 读写分发与目录操作

```c
int vfs_read(struct file *fp, char *buf, size_t count) {
    if (!fp || !fp->used || !fp->inode) return -1;
    return tmpfs_ops.read(fp, buf, count);
}

int vfs_write(struct file *fp, const char *buf, size_t count) {
    if (!fp || !fp->used || !fp->inode) return -1;
    return tmpfs_ops.write(fp, buf, count);
}

int vfs_mkdir(const char *path) {
    if (!path || path[0] != '/') return -1;

    char buf[MAX_PATH];
    strncpy(buf, path, MAX_PATH - 1);
    buf[MAX_PATH - 1] = '\0';

    char *parent_path, *name;
    split_path(buf, &parent_path, &name);
    if (!name[0]) return -1;

    struct inode *parent = vfs_path_lookup(parent_path);
    if (!parent || parent->type != FS_TYPE_DIR) return -1;

    struct inode *d = tmpfs_create(parent, name, FS_TYPE_DIR);
    return d ? 0 : -1;
}

int vfs_list(const char *path) {
    struct inode *dir = vfs_path_lookup(path);
    if (!dir || dir->type != FS_TYPE_DIR) return -1;

    for (struct inode *c = dir->children; c; c = c->next) {
        uart_puts(c->name);
        if (c->type == FS_TYPE_DIR) uart_putc('/');
        uart_puts("\n");
    }
    return 0;
}
```

`vfs_read`/`vfs_write` 只是包装方法表调用；`vfs_list` 打印目录内容，目录名后带 `/`。

## 五、初始化与预置目录

```c
void vfs_init(void) {
    root_inode = alloc_inode("/", FS_TYPE_DIR, NULL);

    vfs_mkdir("/home");
    vfs_mkdir("/etc");

    // 预置欢迎文件
    struct file *fp = vfs_open("/etc/motd", O_CREAT | O_WRONLY);
    if (fp) {
        const char *msg = "Welcome to MyOS! Type 'help' for commands.\n";
        vfs_write(fp, msg, strlen(msg));
        vfs_close(fp);
    }

    uart_puts("VFS (tmpfs) initialized\n");
}
```

启动后文件树：

```
/
├── home/
└── etc/
    └── motd  ("Welcome to MyOS! Type 'help' for commands.\n")
```

shell 的 `cat /etc/motd` 就能读到欢迎语。


`vfs_init` 演示了文件系统初始化的标准流程：先建根 inode（`/`，类型目录），再用公共接口建 `/home`、`/etc` 两个预置目录，最后通过"打开 + 写入 + 关闭"三步写 motd——注意这里用的是 `vfs_open` 的 `O_CREAT | O_WRONLY` 标志组合，与 shell 用户程序里的调用方式完全一致，说明 VFS 接口对内核内部与用户程序一视同仁。motd 的内容 `"Welcome to MyOS! Type 'help' for commands.\n"` 正是 shell 启动时 `cat /etc/motd` 打出的欢迎语（第 16 章），文件系统与 shell 由此闭环。
## 六、string.h：无 libc 的自给自足

tmpfs 大量使用字符串操作，但内核不链接 libc。`include/string.h` 提供内联实现：

```c
// include/string.h
#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <stdint.h>

static inline void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

static inline void *memset(void *dest, int c, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dest;
}

static inline int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}

// strlen/strcpy/strncpy/strcmp 等略
#endif
```

`#include <stdint.h>` 提供 `uint8_t` 等固定宽度类型——这是编译 v0.3 时踩过的坑：早期版本漏了这个头文件，`tmpfs.c` 编译直接报 `unknown type name 'uint8_t'`。


`string.h` 用 `static inline` 定义 memcpy/memset/memcmp 而不是普通函数，有明确的工程原因：内联函数在每个使用点原地展开，免去函数调用开销；`static` 限定使每个编译单元各自持有一份定义，避免多编译单元链接时的重复符号错误。作为头文件提供的"库"，`static inline` 是免链接、免符号表的标准手法。实现上 memcpy/memset/memcmp 都是逐字节循环——正确性优先、性能朴素；真实内核的 `memcpy` 在 ARM64 上由汇编优化（按 16 字节批量拷贝），但语义与此一致。`memcmp` 返回 `pa[i] - pb[i]` 的字节差而非简单的 0/1，这是与 libc 一致的约定（strcmp 族同款），`tmpfs_lookup` 的 `strcmp == 0` 判断依赖这个约定。
## 七、文件系统与系统调用对接

| 系统调用 | VFS 函数 |
|---|---|
| sys_open | vfs_open |
| sys_close | vfs_close |
| sys_read | vfs_read |
| sys_write | vfs_write |
| sys_ls | vfs_list |
| sys_mkdir | vfs_mkdir |

`cat` 命令的完整路径：`sys_open("/etc/motd")` → `sys_read` 循环 → `sys_close`。


这张对照表浓缩了第 12 章到本章的对接：系统调用层只做参数搬运（从异常帧取路径、把返回值写回 x0），VFS 层负责路径解析与文件表管理，tmpfs 方法表负责具体读写。`cat` 的完整调用链是全书一条最长的纵向路径：shell 的 `cat` 函数 → `_syscall3` 内联汇编 svc → 向量表 → `handle_syscall` → `sys_open` → `vfs_open` → `vfs_path_lookup`（逐级 lookup）→ `tmpfs_lookup`（链表扫描）→ 找到 motd 的 inode → 返回 file 槽位指针 → 回用户态拿到 fd → `sys_read` → `vfs_read` → `tmpfs_read`（memcpy）→ 用户缓冲区 → 循环读到 EOF（返回 0）→ `sys_close`。读者在 GDB 里沿这条链下断点，能看到一次 `cat` 从用户态到内核再到文件数据的完整旅程——这正是"系统调用是全部内核机制的汇聚点"的直观证据。
## 八、小结

本章以 tmpfs 为实例完整实现了文件系统的骨架。数据结构层面：inode 树用 children 链表统一组织目录与文件，静态 file 表用 used 标志实现"指针即 fd"，`fs_operations` 方法表为多文件系统预留接口。实现层面：`alloc_inode` 负责 inode 生命周期，`tmpfs_lookup`/`tmpfs_create` 实现目录查找与重名拒绝，`tmpfs_read`/`tmpfs_write` 实现带偏移的文件读写与按需扩容。VFS 层：`vfs_path_lookup` 逐级解析绝对路径，`split_path` 拆分父路径与末级名，`vfs_open` 实现 O_CREAT 两阶段语义，`vfs_mkdir`/`vfs_list` 覆盖目录操作。工程细节：`string.h` 用 static inline 提供无 libc 字符串原语，v0.3 编译踩过的 `uint8_t` 未声明坑源于漏 `#include <stdint.h>`。最后通过"系统调用 ↔ VFS ↔ tmpfs"三层对照，把 `cat /etc/motd` 的完整调用链串成全书最长的一条纵向路径。
## 九、练习

练习一，实现 `vfs_remove` 删除空文件：需要先在 `fs_operations` 里补 `unlink` 方法（在 children 链表里摘除节点并 `kfree` inode），再在 `tmpfs_ops` 填充实现，然后在 shell 加 `rm` 命令（第 16 章的命令框架），最后处理"文件被打开时删除"的语义（提示：用 refcount）。练习二，给 inode 增加 `mtime` 字段，在 `tmpfs_write` 里更新时间戳，扩展 `vfs_list` 或新增 `ls -l` 打印修改时间——顺带体会"元数据与数据分开管理"的文件系统设计。练习三，实现 `vfs_rename`（把 inode 从旧父目录摘除、挂到新父目录并改名），验证重命名后已打开的文件（持有 inode 指针的 file）是否仍然可读——这个实验会让你理解"路径是名字、inode 是实体"的分离，正是 Linux 中 rename 与 open 文件互不干扰的原因。
