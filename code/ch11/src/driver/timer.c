// src/driver/timer.c
#include "timer.h"
#include "uart.h"
#include "irq.h"
#include "proc.h"
#include <stddef.h>

static void timer_interrupt_handler(int irq, void *data);

static unsigned long timer_freq;
static unsigned long tick_count = 0;
static unsigned int timer_hz = 100;

void timer_init(unsigned int hz) {
    timer_hz = hz;

    asm volatile("mrs %0, cntfrq_el0" : "=r"(timer_freq));

    uart_puts("Timer frequency: ");
    uart_puthex((unsigned int)timer_freq);
    uart_puts(" Hz\n");

    unsigned long interval = timer_freq / hz;
    asm volatile("msr cntv_tval_el0, %0" :: "r"(interval));

    unsigned long ctl = 1;  // ENABLE=1, IMASK=0
    asm volatile("msr cntv_ctl_el0, %0" :: "r"(ctl));

    irq_register(27, timer_interrupt_handler, NULL);

    uart_puts("Timer initialized at ");
    uart_puthex(hz);
    uart_puts(" Hz\n");
}

static void timer_interrupt_handler(int irq, void *data) {
    (void)irq; (void)data;
    tick_count++;

    unsigned long interval = timer_freq / timer_hz;
    asm volatile("msr cntv_tval_el0, %0" :: "r"(interval));

    proc_tick();   // 唤醒到期进程 + 请求抢占
}

unsigned long get_ticks(void) {
    return tick_count;
}

unsigned long get_frequency(void) {
    return timer_freq;
}

unsigned int timer_get_hz(void) {
    return timer_hz;
}

void udelay(unsigned long us) {
    unsigned long freq = timer_freq;
    unsigned long start;
    asm volatile("mrs %0, cntpct_el0" : "=r"(start));
    unsigned long target = start + (freq / 1000000) * us;
    unsigned long current;
    do {
        asm volatile("mrs %0, cntpct_el0" : "=r"(current));
    } while (current < target);
}
