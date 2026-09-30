// include/mm.h
#ifndef MM_H
#define MM_H

#define PAGE_SIZE       4096
#define PAGE_SHIFT      12
#define PAGE_MASK       (~(PAGE_SIZE - 1))

// 内存布局
#define MEMORY_START    0x40000000
#define MEMORY_SIZE     (128 * 1024 * 1024)  // 128MB
#define MEMORY_END      (MEMORY_START + MEMORY_SIZE)

// 内核镜像结束地址（由链接脚本定义）
extern char _end[];

void page_alloc_init(void);
void *alloc_page(void);
void free_page(void *addr);
unsigned long get_free_page_count(void);

// 工具宏
#define ALIGN_UP(x, a)   (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define PAGE_ALIGN(x)    ALIGN_UP(x, PAGE_SIZE)

#endif
