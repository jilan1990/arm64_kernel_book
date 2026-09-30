// src/boot/start.S
// 内核入口：从 QEMU 启动，降级到 EL1，清零 BSS，调用 kernel_main

.section .text.start
.global _start

_start:
    // 第一步：从 EL2 降级到 EL1（如果当前在 EL2）
    mrs x0, CurrentEL
    cmp x0, #0x8
    b.ne .Lel1_ok

    // 配置 HCR_EL2
    ldr x0, =(1 << 31)
    msr hcr_el2, x0

    // 配置 SPSR_EL2：EL1h，屏蔽所有中断
    ldr x0, =0x3c5
    msr spsr_el2, x0

    // 设置返回地址
    adr x0, .Lel1_ok
    msr elr_el2, x0

    eret

.Lel1_ok:
    // 第二步：设置 EL1 栈指针
    ldr x0, =_stack_top
    mov sp, x0

    // 第三步：清零 BSS 段
    ldr x0, =__bss_start
    ldr x1, =__bss_end
    bl clear_bss

    // 第四步：保存 X0 中的设备树地址（如果有）
    ldr x1, =dtb_addr
    str x0, [x1]

    // 第五步：调用 C 主函数
    bl kernel_main

    // 如果返回，死循环
.Lhang:
    b .Lhang

// 清零 BSS 段
clear_bss:
    cmp x0, x1
    b.eq .Lbss_done
    str xzr, [x0], #8
    b clear_bss
.Lbss_done:
    ret

// 设备树地址变量
.section .data
.global dtb_addr
dtb_addr:
    .quad 0
