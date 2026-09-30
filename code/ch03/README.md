# ch03 ARM64 架构基础 — 本章代码快照

## 本章主题
讲解 ARM64 通用寄存器、异常级别模型（EL0–EL3）、系统寄存器、常用汇编指令、内存模型与函数调用约定（AAPCS64）。

## 本章无独立内核代码
本章是纯架构知识章节，不包含可编译的内核工程文件。下一章（ch04）将开始写第一个裸机 Hello World 程序。

---

### 汇编示例（非构建文件，摘自书稿）

#### 函数序言与尾声（AAPCS64 约定）
```assembly
function:
    stp x29, x30, [sp, #-16]!   // 保存 FP 和 LR，SP 减 16
    mov x29, sp                 // 设置 FP
    sub sp, sp, #32             // 分配 32 字节局部变量空间
    // ... 函数体 ...
    add sp, sp, #32             // 释放局部变量空间
    ldp x29, x30, [sp], #16     // 恢复 FP 和 LR，SP 加 16
    ret                         // 返回
```

#### 内存复制 memcpy（后索引寻址 + 条件分支示例）
```assembly
.global memcpy
memcpy:
    mov x3, x0
1:
    cmp x2, #8
    b.lo 2f
    ldr x4, [x1], #8
    str x4, [x0], #8
    sub x2, x2, #8
    b 1b
2:
    cbz x2, 3f
    ldrb w4, [x1], #1
    strb w4, [x0], #1
    sub x2, x2, #1
    b 2b
3:
    mov x0, x3
    ret
```

#### 读写系统寄存器（内联汇编示例）
```c
unsigned long el;
asm volatile("mrs %0, CurrentEL" : "=r"(el));
```

### 关键寄存器速查（非构建文件，摘自书稿）

| 寄存器 | 用途 |
|---|---|
| X0–X7 | 函数参数 / 返回值（X0） |
| X8 | 间接结果 / 系统调用号 |
| X9–X15 | 临时寄存器（caller-saved） |
| X19–X28 | 被调用者保存（callee-saved） |
| X29 (FP) | 帧指针 |
| X30 (LR) | 链接寄存器（返回地址） |
| XZR/WZR | 零寄存器 |
| SP | 栈指针（EL1 内核栈用 SP_EL1） |

### 异常级别
- **EL0**：用户态，运行应用程序
- **EL1**：内核态，运行操作系统内核
- **EL2**：Hypervisor（QEMU 启动默认在此，需降级到 EL1）
- **EL3**：安全监控级（TrustZone）

### 下一章起点
ch04 将实现 PL011 UART 驱动和最小 main.c，让内核通过串口输出 "Hello, MyOS!"。
