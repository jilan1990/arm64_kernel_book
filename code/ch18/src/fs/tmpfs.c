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
    if (tmpfs_lookup(dir, name)) return NULL;

    struct inode *ino = alloc_inode(name, type, dir);
    if (!ino) return NULL;

    ino->next = dir->children;
    dir->children = ino;
    return ino;
}

static int tmpfs_read(struct file *fp, char *buf, size_t count) {
    struct inode *ino = fp->inode;
    if (!ino || ino->type != FS_TYPE_FILE) return -1;

    if (fp->offset >= ino->size) return 0;

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

static struct fs_operations tmpfs_ops = {
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .unlink = NULL,
    .read = tmpfs_read,
    .write = tmpfs_write,
};

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
    return NULL;
}

int vfs_close(struct file *fp) {
    if (!fp || !fp->used) return -1;
    fp->used = 0;
    fp->inode = NULL;
    return 0;
}

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
