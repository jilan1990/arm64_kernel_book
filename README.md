# 从零实现 QEMU ARM64 操作系统内核（v0.3 版）

本书以「知识点讲解 + 对应源码解读」为主线，从零实现一个运行在 QEMU `virt` 模拟器上的 ARM64 操作系统内核（MyOS v0.3）。

## 内容结构

全书共 19 章，按「裸机起步 → 异常与中断 → 内存管理 → 进程与调度 → 系统调用与同步 → 驱动与文件系统 → 用户态 shell → 网络协议栈」的顺序递进：

| 章节 | 主题 |
|---|---|
| 第 1 章 | 引言：为什么从零写一个操作系统 |
| 第 2 章 | 开发环境与工具链 |
| 第 3 章 | ARM64 体系结构基础 |
| 第 4 章 | 第一个裸机程序：Hello World |
| 第 5 章 | 启动与链接脚本 |
| 第 6 章 | 异常与中断 |
| 第 7 章 | 物理内存管理：页分配器 |
| 第 8 章 | 虚拟内存与 MMU |
| 第 9 章 | 内核分配器：slab |
| 第 10 章 | 进程管理 |
| 第 11 章 | 调度器与定时器 |
| 第 12 章 | 系统调用 |
| 第 13 章 | 同步原语：自旋锁与信号量 |
| 第 14 章 | 设备驱动模型：字符与块设备 |
| 第 15 章 | 文件系统：tmpfs 与 VFS |
| 第 16 章 | 用户态 shell 与 fork/exec |
| 第 17 章 | 综合实验：三进程并发 |
| 第 18 章 | 网络协议栈与 virtio-net |
| 第 19 章 | 总结与展望 |

## 目录说明

- `manuscript/`：19 章正文（Markdown），每章代码块与 `code/` 中源码逐字一致。
- `final.md`：全书合并终稿。
- `outline.md` / `chapter-ledger.md`：章节规划与字数台账。
- `sources.md`：素材来源说明。
- `code/`：v0.3 内核完整源码（即书中讲解的真源，构建与运行方式见下）。

## 构建与运行

```bash
cd code
make            # 编译生成 kernel.elf
make run        # 用 QEMU 启动（含 virtio-net 网络设备）
```

QEMU 版本建议 9.0+；交叉工具链为 `aarch64-linux-gnu-gcc`。

启动后进入交互 shell，支持 `help / echo / ps / ls / mkdir / cat / run / pid / clear / exit` 等命令；内置 `hello`（打印 3 次后睡眠）、`test`（系统调用自检）与 `net`（UDP DNS 查询）三个用户程序，可通过 `run hello`、`run test`、`run net` 执行——`run net` 会向 QEMU 用户网络内置 DNS（10.0.2.3:53）查询 example.com，真实返回其 A 记录（如 104.20.23.154）。
