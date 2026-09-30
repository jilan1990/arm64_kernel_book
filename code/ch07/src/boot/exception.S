// src/boot/exception.S
// ARM64 异常向量表与异常处理汇编

.section .text.vectors
.global vectors
.global vectors_base
.global interrupt_return

// 异常帧大小：31 个通用寄存器 + SP_EL0 + ELR_EL1 + SPSR_EL1 = 34 * 8 = 272 字节
.equ EXC_FRAME_SIZE, 272

// 保存所有通用寄存器到栈
.macro save_all
    sub sp, sp, #EXC_FRAME_SIZE
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x0, sp_el0
    str x0, [sp, #248]
    mrs x0, elr_el1
    str x0, [sp, #256]
    mrs x0, spsr_el1
    str x0, [sp, #264]
.endm

// 从栈恢复所有寄存器并返回
// 注意：sp_el0/elr/spsr 用 x2 作暂存，最后再重载 x2/x3，避免破坏已恢复的用户寄存器
.macro restore_all
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    ldr x2, [sp, #248]
    msr sp_el0, x2
    ldr x2, [sp, #256]
    msr elr_el1, x2
    ldr x2, [sp, #264]
    msr spsr_el1, x2
    ldp x2, x3, [sp, #16]
    add sp, sp, #EXC_FRAME_SIZE
    eret
.endm

// 向量表项宏：对齐到 128 字节，放一条跳转，其余填 NOP
.macro ventry handler
.align 7
    b \handler
    .rept 31
    nop
    .endr
.endm

// ========== 异常向量表 ==========
.align 11
vectors:
vectors_base:

// 当前 EL 使用 SP_EL0
ventry sync_handler
ventry irq_handler
ventry fiq_handler
ventry error_handler

// 当前 EL 使用 SP_ELx
ventry sync_handler_kern
ventry irq_handler
ventry fiq_handler
ventry error_handler

// 更低 EL 使用 AArch64（用户程序 svc/中断走这里）
ventry sync_handler
ventry irq_handler
ventry fiq_handler
ventry error_handler

// 更低 EL 使用 AArch32
ventry sync_handler
ventry irq_handler
ventry fiq_handler
ventry error_handler

// ========== 异常处理入口 ==========
sync_handler:
    save_all
    mov x0, sp
    bl do_sync_handler
    restore_all

sync_handler_kern:
    save_all
    mov x0, sp
    bl do_sync_handler
    restore_all

irq_handler:
    save_all
    mov x0, sp
    bl irq_handler_c
    restore_all

fiq_handler:
    b .

error_handler:
    b .

// 从当前栈上的异常帧直接 eret 返回。
// 用途：新创建的进程首次被调度、以及被抢占的进程再次被调度时，
// context.sp 指向异常帧、context.x30 指向这里。
interrupt_return:
    restore_all
