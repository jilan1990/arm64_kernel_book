# ch05 启动代码与链接脚本 — 本章代码快照

## 本章主题
编写汇编启动代码（EL2→EL1 降级、栈初始化、BSS 清零）和链接脚本，链接出第一个可启动的 `kernel.elf`。

## 新增文件（相对上一章）与新增能力
- 新增文件：
  - `src/boot/start.S` — 内核入口：检测 CurrentEL、从 EL2 降级到 EL1、设置栈指针、清零 BSS、保存 DTB 地址、调用 `kernel_main`
  - `linker.ld` — 链接脚本：内核加载基址 `0x40080000`、段布局（.text/.rodata/.data/.bss）、预留 16KB 内核栈
- 新增能力：
  - CPU 上电后从 `_start` 开始执行，自动处理异常级别降级
  - 完整 C 运行时环境（栈就绪、BSS 清零）
  - 链接生成可启动的 `kernel.elf`，QEMU 可直接加载运行

## 编译与运行
```bash
cd .../code-by-chapter/ch05
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
```

编译产物：`kernel.elf` + `kernel.dis`（反汇编）。

QEMU 运行命令（沙箱无 QEMU，未实机验证；预期输出基于书稿第四章记录与源码分析）：
```bash
qemu-system-aarch64 -M virt -m 256M -nographic -cpu cortex-a57 -kernel kernel.elf
```

预期串口输出：
```
Hello, MyOS!
Chapter 4: UART Hello World
```

## 已引入但暂未链接/执行的模块
无（本章 start.S + uart.c + main.c 全部参与链接，kernel.elf 可独立运行）。

## 尚未包含（下一章起才出现）的模块
- `src/boot/exception.S` — 异常向量表（ch06）
- `src/kernel/irq.c` — 中断处理（ch06）
- `src/driver/gic.c` — GIC 中断控制器驱动（ch06）
- 物理内存分配器、slab、进程管理、文件系统、用户 Shell、网络栈等后续章节模块

## 关于 main.c 的特别说明
`src/kernel/main.c` 仍使用书稿第四章的最小教学版（9 行）。最终版 `kernel_main`（含完整初始化链）将在 ch18 引入。本章 main.c 与 code/ 真源的最终版不同，这是预期行为。
