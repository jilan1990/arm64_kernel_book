// include/spinlock.h
#ifndef SPINLOCK_H
#define SPINLOCK_H

typedef struct {
    volatile unsigned long locked;
} spinlock_t;

#define SPIN_LOCK_INITIALIZER { 0 }

void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
int spin_trylock(spinlock_t *lock);

#endif
