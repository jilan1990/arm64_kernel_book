// include/syscall.h
#ifndef SYSCALL_H
#define SYSCALL_H

#include "exception.h"

// 系统调用号
#define SYS_write   1
#define SYS_read    2
#define SYS_exit    3
#define SYS_fork    4
#define SYS_execve  5
#define SYS_sleep   6
#define SYS_getpid  7
#define SYS_open    8
#define SYS_close   9
#define SYS_ls      10
#define SYS_ps      11
#define SYS_mkdir   12
#define SYS_wait    13
#define SYS_net_socket 14
#define SYS_net_send  15
#define SYS_net_recv  16

#define MAX_SYSCALL 32

typedef long (*syscall_handler_t)(struct exception_frame *frame);

void syscall_init(void);
void handle_syscall(struct exception_frame *frame);

#endif
