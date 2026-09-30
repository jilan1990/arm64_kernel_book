// include/timer.h
#ifndef TIMER_H
#define TIMER_H

void timer_init(unsigned int hz);
void timer_enable(void);
void timer_disable(void);
unsigned long get_ticks(void);
unsigned long get_frequency(void);
unsigned int timer_get_hz(void);
void udelay(unsigned long us);

#endif
