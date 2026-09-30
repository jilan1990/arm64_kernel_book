// include/semaphore.h
#ifndef SEMAPHORE_H
#define SEMAPHORE_H

struct task_struct;

typedef struct {
    int count;
    struct task_struct *wait;  // 等待队列
} semaphore_t;

void sema_init(semaphore_t *sem, int count);
void sema_down(semaphore_t *sem);
int sema_try_down(semaphore_t *sem);
void sema_up(semaphore_t *sem);

#endif
