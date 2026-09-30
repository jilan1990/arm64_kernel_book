// src/driver/gic.c
#include "gic.h"

#define GICD_READ(reg)  (*(volatile unsigned int *)(GIC_DIST_BASE + (reg)))
#define GICD_WRITE(reg, val) (*(volatile unsigned int *)(GIC_DIST_BASE + (reg)) = (val))
#define GICC_READ(reg)  (*(volatile unsigned int *)(GIC_CPU_BASE + (reg)))
#define GICC_WRITE(reg, val) (*(volatile unsigned int *)(GIC_CPU_BASE + (reg)) = (val))

void gic_init(void) {
    // 启用分发器
    GICD_WRITE(GICD_CTLR, 0x1);

    // 启用 CPU 接口
    GICC_WRITE(GICC_CTLR, 0x1);

    // 设置优先级掩码为最低优先级（允许所有中断）
    GICC_WRITE(GICC_PMR, 0xff);
}

void gic_enable_irq(int irq) {
    // 每个寄存器管理 32 个中断
    int reg = irq / 32;
    int bit = irq % 32;
    GICD_WRITE(GICD_ISENABLER + reg * 4, (1 << bit));
}

void gic_disable_irq(int irq) {
    int reg = irq / 32;
    int bit = irq % 32;
    GICD_WRITE(GICD_ICENABLER + reg * 4, (1 << bit));
}

int gic_acknowledge(void) {
    return GICC_READ(GICC_IAR) & 0x3ff;  // 低 10 位是中断号
}

void gic_end_of_interrupt(int irq) {
    GICC_WRITE(GICC_EOIR, irq & 0x3ff);
}
