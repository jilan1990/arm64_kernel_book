# ch04 第一个裸机程序：Hello World — 本章代码快照

## 本章主题
实现 PL011 UART 驱动，编写最小 main.c，验证交叉编译链路可用。

## 新增文件（相对上一章）与新增能力
- 新增文件：
  - `include/uart.h` — UART 寄存器定义与函数声明
  - `src/driver/uart.c` — PL011 UART 驱动（轮询发送、阻塞接收、hex 输出）
  - `src/kernel/main.c` — 最小教学版 `kernel_main`（9 行，书稿第四章原样）
- 新增能力：
  - PL011 UART MMIO 驱动（`uart_init` / `uart_putc` / `uart_puts` / `uart_puthex` / `uart_getc`）
  - 裸机编译选项验证（`-ffreestanding -nostdlib -mgeneral-regs-only`）
  - 最小入口函数 `kernel_main` 可编译

## 编译与运行
```bash
cd .../code-by-chapter/ch04
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
```

本章 `make` 只编译 `uart.o` 和 `main.o` 两个目标文件，**不链接 kernel.elf**（无入口点、无链接脚本）。

预期输出（编译阶段，无运行输出；沙箱无 QEMU，未实机验证）：
```
CC src/driver/uart.o
CC src/kernel/main.o
```

### 关于"可启动镜像"的说明
本章快照验证 UART 驱动与最小入口函数**可编译**；要生成可启动的 `kernel.elf`，需要第 5 章的 `start.S`（异常级别降级 + BSS 清零 + 跳转 C）和 `linker.ld`（段布局 + 入口地址）。书稿第四章所述"能跑就行的启动代码"未保留在真源中，故不重建。

## 已引入但暂未链接/执行的模块
无（本章只编译两个 .o 文件，不链接）。

## 尚未包含（下一章起才出现）的模块
- `src/boot/start.S` — 汇编启动代码（EL2→EL1 降级、栈初始化、BSS 清零）
- `linker.ld` — 链接脚本（内核镜像内存布局）
- 可启动的 `kernel.elf` 链接产物

## 关于 main.c 的特别说明
`src/kernel/main.c` 是全书唯一按书稿演进的文件。本章及 ch05–ch17 使用书稿第四章的最小教学版（9 行），ch18 起才换成最终版（含完整初始化链）。本章 main.c 与 code/ 真源的最终版不同，这是预期行为。
