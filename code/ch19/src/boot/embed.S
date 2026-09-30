# src/boot/embed.S -- 把用户程序二进制嵌入内核镜像
.section .rodata.embed

.balign 8
.global shell_prog_start
.global shell_prog_end
shell_prog_start:
.incbin "user/shell.bin"
shell_prog_end:

.balign 8
.global hello_prog_start
.global hello_prog_end
hello_prog_start:
.incbin "user/hello.bin"
hello_prog_end:

.balign 8
.global test_prog_start
.global test_prog_end
test_prog_start:
.incbin "user/test.bin"
test_prog_end:

.balign 8
.global net_prog_start
.global net_prog_end
net_prog_start:
.incbin "user/net.bin"
net_prog_end:
