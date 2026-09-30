# code-by-chapter — 《从零实现 ARM64 操作系统内核》逐章代码快照

本目录为全书 19 章的**逐章代码快照**：每章目录是一个独立工程（源文件 + 裁剪后的 Makefile + README.md），代码随章节内容步步演进——ch04 最小裸机程序，逐步叠加启动、异常、内存、进程、调度、系统调用、同步、驱动、文件系统、用户空间，至 ch18 形成完整内核，ch19 为最终全量。

## 演进模型（必须了解的三条规则）

1. **文件集合单调递增**：chN 的文件集合 ⊆ chN+1 ⊆ … ⊆ ch19 = `code/` 全量。演进只表现为"文件集合增长 + 每章 README 的新增能力说明"。
2. **与真源逐字一致**：每章快照中的每个文件与书稿根 `code/` 同名文件 diff 零差异。**唯一例外是 `src/kernel/main.c`**：ch04–ch17 使用书稿第四章的最小教学版（9 行，仅串口输出），ch18–ch19 换成最终版（完整初始化链）——这是书稿自身记载的演进，也是全书唯一内容发生变化的文件。
3. **可链接子集**：每章 Makefile 使用显式源文件表，只链接"该章符号可解析"的文件。书稿各章展示的是最终版代码，部分文件（`exception.S/irq.c/proc.c/timer.c/syscall.c/semaphore.c`）相互引用、又依赖后续章符号，构成互锁簇，只有 ch18 全量时才全部链接进内核。未链接的文件**保留在快照中**（与真源逐字一致），README 注明原因与生效章。

## 19 章演进总表

> "能否运行"列：QEMU 运行模板 + 预期输出见各章 README；因生成环境无可用 QEMU，预期输出基于书稿记录与代码分析，**未实机验证**。

| 章 | 主题 | 新增文件数 | 累计文件数 | 编译 | 运行 |
|---|---|---|---|---|---|
| ch01 | 导论 | 0 | 0 | — | 无代码（仅 README） |
| ch02 | 开发环境搭建 | 0 | 0 | — | 无代码（环境命令见 README） |
| ch03 | ARM64 架构基础 | 0 | 0 | — | 无代码（汇编示例为非构建文件） |
| ch04 | 第一个裸机程序：Hello World | 3 | 3 | ✅ 编译 uart.o/main.o | ⚠️ 无入口/链接脚本，不可启动（见 README） |
| ch05 | 启动代码与链接脚本 | 2 | 5 | ✅ kernel.elf | ✅ 输出 Hello, MyOS! / Chapter 4: UART Hello World |
| ch06 | 异常与中断处理 | 6 | 11 | ✅ | ✅（异常/中断簇待 ch18 生效） |
| ch07 | 物理内存管理 | 2 | 13 | ✅ | ✅（页分配器已链接，未被调用） |
| ch08 | 内存布局与 MMU 设计 | 2 | 15 | ✅ | ✅ |
| ch09 | 内核内存分配器 | 2 | 17 | ✅ | ✅（slab 已链接，未被调用） |
| ch10 | 进程管理与上下文切换 | 2 | 19 | ✅ | ✅（proc.c 待 ch18 生效） |
| ch11 | 调度器与定时器 | 2 | 21 | ✅ | ✅（timer.c 待 ch18 生效） |
| ch12 | 系统调用 | 4 | 25 | ✅ | ✅（syscall.c 待 ch18 生效） |
| ch13 | 同步原语 | 4 | 29 | ✅ | ✅（spinlock 已链接；semaphore 待 ch18） |
| ch14 | 设备驱动模型 | 5 | 34 | ✅ | ✅（字符/块设备框架已链接） |
| ch15 | 文件系统 | 3 | 37 | ✅ | ✅（tmpfs 已链接，未被调用） |
| ch16 | 用户空间与 Shell | 5 | 42 | ✅ kernel.elf + 4 个用户程序 | ✅（内嵌程序已构建，proc.c 未链接故暂不执行） |
| ch17 | 综合实验 | 0 | 42 | ✅ | ✅（书稿以最终代码演示，快照按演进不含 ch18 网络代码，见 README） |
| ch18 | 网络支持 | 12 | 54 | ✅ 完整内核 | ✅ 完整启动链 + Shell + run hello/test/net |
| ch19 | 总结 | 1 | 55 | ✅ code/ 全量 | ✅ 同 ch18（含 code/README.md） |

注：累计文件数只计源码文件（include/、src/、user/），不含 Makefile/README.md。ch19 新增的 1 个文件是 `user/syscall.h`（真源遗留头文件，无程序引用，随"code/ 全量"纳入）。

## 工具链与构建

交叉工具链（已验证可用）：

```bash
CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
cd code-by-chapter/chXX
make CROSS=$CROSS        # 编译/链接（每章已实测 exit 0 无 error）
make run                 # QEMU 运行（需本机安装 qemu-system-aarch64）
make clean
```

QEMU 运行模板（网络设备仅 ch18/ch19 需要）：

```bash
# ch05–ch17
qemu-system-aarch64 -M virt -m 256M -nographic -cpu cortex-a57 -kernel kernel.elf
# ch18/ch19（含 virtio-net）
qemu-system-aarch64 -M virt -m 256M -nographic -cpu cortex-a57 \
        -netdev user,id=net0 -device virtio-net-pci,netdev=net0 -kernel kernel.elf
```

## 校验说明

- 每章已执行 `make CROSS=...` 实测编译通过（exit 0，无 error；仅真源自带的 `-Wint-to-pointer-cast` 与链接器 RWX segment 警告）。
- 每章已执行逐文件 diff：除 `src/kernel/main.c`（演进例外）外与 `code/` 同名文件零差异；ch18/ch19 的 Makefile 与 `code/Makefile` 逐字一致。
- 文件集合单调性已校验：ch04 ⊆ ch05 ⊆ … ⊆ ch19。
- 每章交付前已 `make clean`，目录只含源文件 + Makefile + README.md。

## 各章 README 内容

每章 README.md 包含：本章主题一句话；相对上一章"新增文件"清单与"新增能力"；编译命令（`make CROSS=...`）与运行命令（QEMU 模板 + 预期输出）；该章已引入但暂未链接的模块及生效章；下一章起才会出现的模块提示。ch01–ch03 的 README 说明本章无独立代码及下一章起点（书稿示例内容标注"非构建文件"）。
