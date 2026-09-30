// include/gic.h
#ifndef GIC_H
#define GIC_H

#define GIC_DIST_BASE   0x08000000
#define GIC_CPU_BASE    0x08010000

// 分发器寄存器
#define GICD_CTLR       0x000
#define GICD_ISENABLER  0x100
#define GICD_ICENABLER  0x180
#define GICD_ICPENDR    0x280
#define GICD_IPRIORITYR 0x400
#define GICD_ITARGETSR  0x800

// CPU 接口寄存器
#define GICC_CTLR       0x000
#define GICC_PMR        0x004
#define GICC_IAR        0x00C
#define GICC_EOIR       0x010

void gic_init(void);
void gic_enable_irq(int irq);
void gic_disable_irq(int irq);
int gic_acknowledge(void);
void gic_end_of_interrupt(int irq);

#endif
