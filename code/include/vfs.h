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
