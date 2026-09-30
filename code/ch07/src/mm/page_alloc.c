// src/mm/page_alloc.c
#include "mm.h"
#include "uart.h"
#include <stddef.h>

// 物理页总数
#define MAX_PAGES    (MEMORY_SIZE / PAGE_SIZE)

// 位图：每一位代表一个物理页，0=空闲，1=已分配
static unsigned long page_bitmap[MAX_PAGES / (sizeof(unsigned long) * 8)];

// 第一个可分配页的页号（内核镜像之后）
static int first_free_page;

// 总页数和空闲页数
static int total_pages;
static int free_pages;

// 页号转物理地址
static void *page_to_addr(int page) {
    return (void *)(MEMORY_START + page * PAGE_SIZE);
}

// 物理地址转页号
static int addr_to_page(void *addr) {
    return ((unsigned long)addr - MEMORY_START) / PAGE_SIZE;
}

void page_alloc_init(void) {
    // 计算内核结束后的第一个页号
    unsigned long kernel_end = (unsigned long)_end;
    first_free_page = (kernel_end - MEMORY_START) / PAGE_SIZE;
    if (kernel_end % PAGE_SIZE != 0) {
        first_free_page++;  // 对齐到下一页
    }

    total_pages = MAX_PAGES;

    // 初始时，内核之前和内核占用的页标记为已分配
    for (int i = 0; i < first_free_page; i++) {
        page_bitmap[i / (sizeof(unsigned long) * 8)] |=
            (1UL << (i % (sizeof(unsigned long) * 8)));
    }

    // 其余页标记为空闲
    free_pages = total_pages - first_free_page;

    uart_puts("Page allocator initialized: ");
    uart_puthex(total_pages);
    uart_puts(" total pages, ");
    uart_puthex(free_pages);
    uart_puts(" free pages\n");
}

void *alloc_page(void) {
    // 从 first_free_page 开始扫描空闲页
    for (int i = first_free_page; i < total_pages; i++) {
        int idx = i / (sizeof(unsigned long) * 8);
        int bit = i % (sizeof(unsigned long) * 8);

        if (!(page_bitmap[idx] & (1UL << bit))) {
            // 找到空闲页，标记为已分配
            page_bitmap[idx] |= (1UL << bit);
            free_pages--;
            return page_to_addr(i);
        }
    }

    // 没有空闲页
    uart_puts("alloc_page: out of memory!\n");
    return NULL;
}

void free_page(void *addr) {
    int page = addr_to_page(addr);

    if (page < 0 || page >= total_pages) {
        uart_puts("free_page: invalid address\n");
        return;
    }

    int idx = page / (sizeof(unsigned long) * 8);
    int bit = page % (sizeof(unsigned long) * 8);

    if (!(page_bitmap[idx] & (1UL << bit))) {
        uart_puts("free_page: page already free\n");
        return;
    }

    page_bitmap[idx] &= ~(1UL << bit);
    free_pages++;
}

unsigned long get_free_page_count(void) {
    return free_pages;
}

// mmu_init stub (real implementation in chapter 8)
void mmu_init(void) {
    // Simplified: MMU not enabled in this build
}
