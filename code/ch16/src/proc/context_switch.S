// src/proc/context_switch.S
// 上下文切换：保存/恢复 callee-saved 寄存器与栈指针
.global context_switch
.global switch_to

// void context_switch(struct cpu_context *old, struct cpu_context *new)
// x0 = old context, x1 = new context
context_switch:
    // 保存当前进程的寄存器到 old context
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x2, sp
    str x2, [x0, #96]

    mov x0, x1
    // 落入 switch_to 恢复新进程

// void switch_to(struct cpu_context *new)
// 只恢复不保存：用于抢占路径（现场已写入 prev->context）
switch_to:
    ldp x19, x20, [x0, #0]
    ldp x21, x22, [x0, #16]
    ldp x23, x24, [x0, #32]
    ldp x25, x26, [x0, #48]
    ldp x27, x28, [x0, #64]
    ldp x29, x30, [x0, #80]
    ldr x2, [x0, #96]
    mov sp, x2
    ret
