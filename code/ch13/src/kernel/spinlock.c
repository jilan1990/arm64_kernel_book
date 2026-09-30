// src/kernel/spinlock.c
#include "spinlock.h"

// ARM64 原子交换：用 LDXR/STXR 实现
static inline unsigned long atomic_xchg(volatile unsigned long *ptr, unsigned long val) {
    unsigned long old;
    asm volatile(
        "1: ldxr %0, [%1]\n"
        "   stxr %w2, %3, [%1]\n"
        "   cbnz %w2, 1b\n"
        : "=&r"(old)
        : "r"(ptr), "r"(0), "r"(val)
        : "memory"
    );
    return old;
}

void spin_lock(spinlock_t *lock) {
    // 尝试原子地把 lock 从 0 改为 1
    while (atomic_xchg(&lock->locked, 1) != 0) {
        // 锁被占用，自旋等待
        // 在循环中可以加 WFE 指令节省功耗
        asm volatile("wfe");
    }
    // 获取锁后，确保之前的内存访问不会重排到临界区内
    asm volatile("dmb ish");
}

void spin_unlock(spinlock_t *lock) {
    // 确保临界区内的内存访问都完成后再释放锁
    asm volatile("dmb ish");
    lock->locked = 0;
    // 唤醒等待的 CPU
    asm volatile("sev");
}

int spin_trylock(spinlock_t *lock) {
    // 尝试获取锁，不等待
    return atomic_xchg(&lock->locked, 1) == 0;
}
