// include/irq.h
#ifndef IRQ_H
#define IRQ_H

#include "exception.h"

typedef void (*irq_handler_t)(int irq, void *data);

void irq_init(void);
int irq_register(int irq, irq_handler_t handler, void *data);
void irq_handler_c(struct exception_frame *frame);

#endif
