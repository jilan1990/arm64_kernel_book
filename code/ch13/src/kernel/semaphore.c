// src/kernel/semaphore.c
#include "semaphore.h"
#include "proc.h"
#include "spinlock.h"

// 信号量等待者用 ~0 表示不按时间唤醒
#define WAIT_FOREVER (~0UL)

static spinlock_t sem_lock = SPIN_LOCK_INITIALIZER;

void sema_init(semaphore_t *sem, int count) {
    sem->count = count;
    sem->wait = NULL;
}

void sema_down(semaphore_t *sem) {
    spin_lock(&sem_lock);
    while (sem->count <= 0) {
        // 加入等待队列并睡眠
        current->wait_next = sem->wait;
        sem->wait = current;
        current->wakeup_tick = WAIT_FOREVER;
        current->state = PROC_SLEEPING;
        spin_unlock(&sem_lock);
        schedule();
        spin_lock(&sem_lock);
    }
    sem->count--;
    spin_unlock(&sem_lock);
}

int sema_try_down(semaphore_t *sem) {
    int ok = 0;
    spin_lock(&sem_lock);
    if (sem->count > 0) {
        sem->count--;
        ok = 1;
    }
    spin_unlock(&sem_lock);
    return ok;
}

void sema_up(semaphore_t *sem) {
    spin_lock(&sem_lock);
    sem->count++;
    // 唤醒一个等待者（其 wakeup_tick = ~0，只能在这里被唤醒）
    if (sem->wait) {
        struct task_struct *p = sem->wait;
        sem->wait = p->wait_next;
        p->wait_next = NULL;
        p->state = PROC_READY;
    }
    spin_unlock(&sem_lock);
}
