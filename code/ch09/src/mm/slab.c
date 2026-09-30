// src/mm/slab.c
#include "slab.h"
#include "mm.h"
#include "uart.h"

#define CACHE_COUNT  10
#define MIN_SIZE     8
#define MAX_SIZE     4096

struct slab {
    struct slab *next;
    int obj_size;
    void *free_list;
    int free_count;
    int total_count;
};

struct kmem_cache {
    size_t obj_size;
    struct slab *slabs;  // 所有 slab 的链表
};

static struct kmem_cache caches[CACHE_COUNT];

// 大小级别表
static const size_t size_table[CACHE_COUNT] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

// 找到不小于 size 的最小级别
static int find_cache_index(size_t size) {
    for (int i = 0; i < CACHE_COUNT; i++) {
        if (size_table[i] >= size) return i;
    }
    return -1;  // 超过最大大小，需要直接分配页
}

// 创建一个新的 slab（用一页）
static struct slab *slab_create(size_t obj_size) {
    void *page = alloc_page();
    if (!page) return NULL;

    struct slab *slab = (struct slab *)page;
    slab->obj_size = obj_size;
    slab->next = NULL;

    // 对象从 slab 描述符之后开始
    char *obj_start = (char *)page + sizeof(struct slab);
    obj_start = (char *)ALIGN_UP((unsigned long)obj_start, obj_size);

    // 计算能放多少个对象
    char *page_end = (char *)page + PAGE_SIZE;
    slab->total_count = (page_end - obj_start) / obj_size;
    slab->free_count = slab->total_count;

    // 构建空闲链表（每个对象的前 8 字节存下一个对象的指针）
    slab->free_list = NULL;
    for (int i = 0; i < slab->total_count; i++) {
        void *obj = obj_start + i * obj_size;
        *(void **)obj = slab->free_list;
        slab->free_list = obj;
    }

    return slab;
}

void slab_init(void) {
    for (int i = 0; i < CACHE_COUNT; i++) {
        caches[i].obj_size = size_table[i];
        caches[i].slabs = NULL;
    }
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    // 大对象直接分配页
    if (size > MAX_SIZE) {
        int pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        void *ptr = alloc_page();
        for (int i = 1; i < pages && ptr; i++) {
            alloc_page();  // 连续分配（简化：不保证连续）
        }
        return ptr;
    }

    int idx = find_cache_index(size);
    if (idx < 0) return NULL;

    struct kmem_cache *cache = &caches[idx];

    // 找一个有空闲对象的 slab
    struct slab *slab = cache->slabs;
    while (slab && slab->free_count == 0) {
        slab = slab->next;
    }

    // 没有空闲 slab，创建新的
    if (!slab) {
        slab = slab_create(cache->obj_size);
        if (!slab) return NULL;
        slab->next = cache->slabs;
        cache->slabs = slab;
    }

    // 从空闲链表取一个对象
    void *obj = slab->free_list;
    slab->free_list = *(void **)obj;
    slab->free_count--;

    return obj;
}

void kfree(void *ptr) {
    if (!ptr) return;

    // 找到对象所属的 slab（对象所在页的起始地址就是 slab）
    struct slab *slab = (struct slab *)ALIGN_DOWN((unsigned long)ptr, PAGE_SIZE);

    // 大对象（多页）直接释放
    // 注意：这里简化处理，实际需要记录分配的页数
    if (slab->obj_size == 0) {
        // 可能是多页分配，暂时不处理
        return;
    }

    // 归还到空闲链表
    *(void **)ptr = slab->free_list;
    slab->free_list = ptr;
    slab->free_count++;
}
