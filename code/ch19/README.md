# MyOS — 教学用小型类 Linux 操作系统（AArch64）

在 QEMU virt 机器上运行的 64 位 ARM 裸机操作系统，覆盖操作系统的主要核心子系统。

## 功能特性

- **启动**：QEMU -kernel 直启，EL2 → EL1 降级，BSS 清零（src/boot/start.S）
- **中断/异常**：AArch64 异常向量表、GICv2 中断控制器、SVC 系统调用分发（src/boot/exception.S, src/kernel/irq.c）
- **内存管理**：物理页位图分配器 + slab 内核对象分配器（src/mm/）
- **进程管理**：进程表、内核线程、EL0 用户进程、优先级抢占调度、时钟 tick 睡眠唤醒、僵尸进程回收、fork/execve/wait（src/proc/）
- **系统调用**：write/read/exit/fork/execve/sleep/getpid/open/close/ls/ps/mkdir/wait（src/kernel/syscall.c）
- **文件系统**：内存 tmpfs + VFS 层，目录、文件读写（src/fs/tmpfs.c）
- **设备驱动**：PL011 UART、virtio-blk 风格 ramdisk、字符设备层（src/driver/）
- **网络**：virtio-net 驱动 + ARP/IP/ICMP/UDP 协议栈（src/kernel/net/，本次未改动）
- **同步**：自旋锁（LDXR/STXR 原子指令）、信号量（真实睡眠/唤醒）（src/kernel/）
- **用户程序**：shell、hello、test，编译后内嵌进内核镜像，按 1MB 槽位加载到 EL0 运行（user/）

## 构建与运行

依赖：aarch64-linux-gnu-gcc、qemu-system-aarch64、make。

    make          # 构建 kernel.elf / kernel.bin 及用户程序
    make run      # 在 QEMU 中运行（串口交互）
    make debug    # -s -S 等待 gdb 连接
    make clean

## Shell 命令

    help / echo / pid / ps / ls / mkdir / cat / clear / exit
    run hello | run test   # fork + execve + wait 运行内嵌用户程序

## 内存布局

    0x40000000  内核镜像（链接基址）
    0x40080000  QEMU 默认内核加载地址
    0x40000000~0x48000000  页分配器管理的 128MB
    0x48000000~0x48800000  用户进程槽位（8 × 1MB：低处代码，栈自槽顶向下）

## 设计要点

- 用户程序使用 PC 相对寻址编译，可加载到任意 1MB 对齐槽位；fork 时整槽拷贝并平移 ELR/SP。
- 被抢占进程的现场保存在异常帧中，再次调度时经 interrupt_return 以 eret 恢复；新进程同样用伪造异常帧进入 EL0。
- 时钟中断（cntv + GIC PPI 27，100Hz）驱动睡眠唤醒与抢占；EOI 在抢占切换前完成，避免中断悬挂。
