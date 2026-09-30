// include/slab.h
#ifndef SLAB_H
#define SLAB_H

#include <stddef.h>

void *kmalloc(size_t size);
void kfree(void *ptr);
void slab_init(void);

#endif
