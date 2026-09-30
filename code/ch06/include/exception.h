// include/exception.h
#ifndef EXCEPTION_H
#define EXCEPTION_H

struct exception_frame {
    // 通用寄存器 x0-x30
    unsigned long x0, x1, x2, x3, x4, x5, x6, x7;
    unsigned long x8, x9, x10, x11, x12, x13, x14, x15;
    unsigned long x16, x17, x18, x19, x20, x21, x22, x23;
    unsigned long x24, x25, x26, x27, x28, x29, x30;
    // 特殊寄存器
    unsigned long sp_el0;    // 用户态栈指针
    unsigned long elr_el1;   // 异常返回地址
    unsigned long spsr_el1;  // 保存的 PSTATE
};

void exception_init(void);
void do_sync_handler(struct exception_frame *frame);

#endif
