# user/test.S -- 汇编用户程序：write + exit
.section .text.start
.global _start
_start:
    // write(1, msg, 13)
    mov x8, #1          // SYS_write
    mov x0, #1          // fd = stdout
    adr x1, msg
    mov x2, #13
    svc #0

    // exit(0)
    mov x8, #3          // SYS_exit
    mov x0, #0
    svc #0

    // 不会到达
    b .

.section .rodata
msg:
    .ascii "Hello, User!\n"
