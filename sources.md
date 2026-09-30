# 参考资料来源

## 检索方向 1：ARM64 架构与启动流程

| 来源 | 日期 | 可用结论 | 目标章节 |
|---|---|---|---|
| [Booting AArch64 Linux - kernel.org](https://www.kernel.org/doc/Documentation/arm64/booting.txt) | 2026-04 | ARM64 异常级别 EL0-EL3 模型；CPU 启动时处于 EL2 或 EL1；x0 传设备树地址 | 第3、5章 |
| [ARM64 Boot Sequence - kernel-internals.org](https://kernel-internals.org/arch/arm64/boot/) | 2026-08 | 上电后从 reset vector 执行，MMU 关闭；EL2→EL1 降级流程 | 第5章 |
| [Switching Exception Levels - learn.arm.com](https://learn.arm.com/learning-paths/embedded-and-microcontrollers/bare-metal/exception-levels/) | 2026-08 | EL3 通过 ERET 降级到 EL1；需配置 SCTLR_EL1、SPSR_EL1、ELR_EL1 | 第3、5章 |
| [Introduction to Bootloaders - labcsmart.com](https://labcsmart.com/introduction-to-bootloaders-from-power-on-to-linux-on-arm-and-arm64/) | 2026-03 | EL0=应用, EL1=内核, EL2=hypervisor, EL3=安全监控；特权级硬件强制 | 第3章 |

## 检索方向 2：QEMU 仿真与裸机开发

| 来源 | 日期 | 可用结论 | 目标章节 |
|---|---|---|---|
| [ARM Assembly Part 20: Bare-Metal OS Kernel - wasilzafar.com](https://www.wasilzafar.com/pages/series/arm-assembly/arm-assembly-20-bare-metal-os-kernel.html) | 2026-05 | QEMU virt 机器：Cortex-A57，128MB RAM，PL011 UART at 0x09000000；虚拟定时器 IRQ INTID 27 | 第2、4、11章 |
| [Running full arm64 system under QEMU - kernel.org](https://cdn.kernel.org/pub/linux/kernel/people/will/docs/qemu/qemu-arm64-howto.html) | 2026-04 | `-kernel` 直接加载内核镜像；`-append` 传内核参数；`-nographic` 串口输出 | 第2章 |
| [Raspberry Pi Bare Bones - OSDev Wiki](https://wiki.osdev.org/ARM_RaspberryPi_Tutorial_C) | 2026-07 | QEMU 支持 raspi3/raspi4b 机器类型；`qemu-system-aarch64 -M raspi3 -serial stdio -kernel kernel8.img` | 第2章 |
| [Kernel Debugging using QEMU and GDB - w4118.github.io](https://w4118.github.io/guides/qemu.html) | 2026-09 | `-s -S` 开启 GDB 调试端口；`-initrd` 加载初始内存盘；`console=ttyAMA0` | 第2章 |

## 检索方向 3：MMU 与虚拟内存

| 来源 | 日期 | 可用结论 | 目标章节 |
|---|---|---|---|
| [ARM Assembly Part 12: MMU & Virtual Memory - wasilzafar.com](https://www.wasilzafar.com/pages/series/arm-assembly/arm-assembly-12-mmu-virtual-memory.html) | 2026-03 | 4级页表遍历；TTBR0_EL1/TTBR1_EL1；TCR_EL1 配置地址空间大小；MAIR_EL1 内存属性 | 第8章 |
| [Memory Layout on AArch64 Linux - docs.kernel.org](https://docs.kernel.org/5.15/arm64/memory.html) | 2026-05 | 4KB 页支持 3 或 4 级页表；39位(512GB)或48位(256TB)虚拟地址；64KB 页支持最多3级 | 第8章 |
| [Quick and Dirty AArch64 MMU setup - dannasman.github.io](https://dannasman.github.io/aarch64-mmu) | 2024-09 | TCR_EL1 位域详解：T0SZ/T1SZ、TG0/TG1、SH0/SH1、ORGN0/IRGN0 | 第8章 |
| [ARM64 Page Tables - kernel-internals.org](https://kernel-internals.org/arch/arm64/page-tables/) | 2026-08 | Linux 页表类型映射：PGD=L0, PUD=L1, PMD=L2, PTE=L3；描述符格式 | 第8章 |

## 检索方向 4：进程调度与系统调用

| 来源 | 日期 | 可用结论 | 目标章节 |
|---|---|---|---|
| [ARM64 Syscall Entry - kernel-internals.org](https://kernel-internals.org/arch/arm64/syscall-entry/) | 2026-04 | SVC #0 触发系统调用；x8=调用号，x0-x5=参数，x0=返回值 | 第12章 |
| [User processes and system calls - jbro885wgu.github.io](https://jbro885wgu.github.io/p1-kernel-arm-tutorial/lesson05/rpi-os/) | 2025-12 | 最小系统调用集：write、clone、malloc；用户态栈与内核态栈分离 | 第12章 |
| [Preemptive Multitasking - fxlin.github.io](https://fxlin.github.io/p1-kernel/exp4b/rpi-os/) | 2024-09 | 时间中断驱动抢占式调度；中断上下文切换；原子临界区 | 第11章 |
| [System Calls Demystified - codermusings.com](https://www.codermusings.com/system-calls-demystified/) | 2026-08 | ARM64 系统调用约定：svc #0，x8 调用号，x0-x5 参数 | 第12章 |

## 检索方向 5：完整教程参考

| 来源 | 日期 | 可用结论 | 目标章节 |
|---|---|---|---|
| [Raspberry Pi OS - s-matyukevich.github.io](https://s-matyukevich.github.io/raspberry-pi-os/) | 2025-10 | 从 Hello World 到虚拟内存、进程管理的完整教程；每课有代码和解释 | 全书参考 |
| [Environment Setup - ohyaan.github.io](https://ohyaan.github.io/os-development/phase1-foundation/01._environment_setup_for_raspberry_pi_os_development/) | 2026-08 | 交叉编译器选择：aarch64-none-elf- 优于 aarch64-linux-gnu-；freestanding 编译选项 | 第2章 |
| [ARM Assembly Series - wasilzafar.com](https://www.wasilzafar.com/pages/series/arm-assembly/) | 2026-06 | ARM64 汇编从基础到裸机 OS 的完整系列；共26部分 | 第3章及全书 |
