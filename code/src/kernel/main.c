// src/kernel/main.c
#include "uart.h"
#include "irq.h"
#include "exception.h"
#include "mm.h"
#include "slab.h"
#include "proc.h"
#include "timer.h"
#include "syscall.h"
#include "vfs.h"
#include "net.h"

// 前向声明
void page_alloc_init(void);
void slab_init(void);
void mmu_init(void);
void proc_init(void);
void syscall_init(void);
void device_init(void);
void vfs_init(void);
void create_test_processes(void);

void kernel_main(void) {
    // 1. 最早期初始化：UART
    uart_init();
    uart_puts("\nMyOS v0.3 booting...\n");

    // 2. 异常和中断
    exception_init();
    irq_init();

    // 3. 物理内存管理
    page_alloc_init();

    // 4. 内核内存分配器
    slab_init();

    // 5. 进程管理
    proc_init();

    // 6. 系统调用
    syscall_init();

    // 7. 设备驱动
    device_init();

    // 8. 文件系统
    vfs_init();

    // 9. 网络支持
    net_init();

    // 10. 创建第一个用户进程（shell）
    create_test_processes();

    // 11. 最后启动时钟中断：此后调度器开始抢占运行 shell
    timer_init(100);

    uart_puts("Boot complete. Starting scheduler...\n");

    // idle 循环：回收僵尸进程，等待中断
    for (;;) {
        proc_reap();
        asm volatile("wfi");
    }
}
