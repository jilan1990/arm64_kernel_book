// src/kernel/irq.c
#include "irq.h"
#include "gic.h"
#include "uart.h"
#include "exception.h"
#include "syscall.h"
#include "proc.h"

#define MAX_IRQ 256

extern char vectors[];  // 异常向量表（exception.S）

void exception_init(void) {
    asm volatile("msr vbar_el1, %0" :: "r"(vectors));
}

static void dump_frame(struct exception_frame *frame) {
    static const char *names[31] = {
        "x0 ", "x1 ", "x2 ", "x3 ", "x4 ", "x5 ", "x6 ", "x7 ",
        "x8 ", "x9 ", "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "x29", "x30"
    };
    unsigned long *regs = &frame->x0;
    for (int i = 0; i < 31; i++) {
        uart_puts(names[i]);
        uart_puts("=");
        uart_puthex64(regs[i]);
        uart_puts((i % 4 == 3) ? "\n" : "  ");
    }
    uart_puts("sp_el0="); uart_puthex64(frame->sp_el0); uart_puts("\n");
}

// 同步异常：SVC 走系统调用，其余打印现场并停机
void do_sync_handler(struct exception_frame *frame) {
    unsigned long esr, far;
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    unsigned long ec = (esr >> 26) & 0x3f;

    if (ec == 0x15) {          // ESR.EC = SVC，AArch64
        handle_syscall(frame);
        return;
    }

    asm volatile("mrs %0, far_el1" : "=r"(far));
    uart_puts("\nSYNC EXCEPTION! EC=");
    uart_puthex((unsigned int)ec);
    uart_puts("\n  ESR: "); uart_puthex64(esr); uart_puts("\n");
    uart_puts("  FAR: "); uart_puthex64(far); uart_puts("\n");
    uart_puts("  ELR: "); uart_puthex64(frame->elr_el1); uart_puts("\n");
    uart_puts("  SPSR: "); uart_puthex64(frame->spsr_el1); uart_puts("\n");
    dump_frame(frame);
    while (1) { }
}

static irq_handler_t irq_handlers[MAX_IRQ];
static void *irq_data[MAX_IRQ];

void irq_init(void) {
    gic_init();
    asm volatile("msr daifclr, #2");   // 开 IRQ
}

int irq_register(int irq, irq_handler_t handler, void *data) {
    if (irq < 0 || irq >= MAX_IRQ) return -1;
    irq_handlers[irq] = handler;
    irq_data[irq] = data;
    gic_enable_irq(irq);
    return 0;
}

void irq_handler_c(struct exception_frame *frame) {
    int irq = gic_acknowledge();

    if (irq >= 1020) return;   // spurious

    if (irq < MAX_IRQ && irq_handlers[irq]) {
        irq_handlers[irq](irq, irq_data[irq]);
    } else {
        uart_puts("Unexpected IRQ: ");
        uart_puthex((unsigned int)irq);
        uart_puts("\n");
    }

    // 必须先 EOI 再抢占，否则该中断一直处于 active，后续中断无法送达
    gic_end_of_interrupt(irq);

    if (need_resched) {
        need_resched = 0;
        preempt_from_frame(frame);
    }
}
