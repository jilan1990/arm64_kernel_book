# ch14 设备驱动模型 — 本章代码快照

## 本章主题
用"设备对象 + 函数指针表 + 主设备号注册"统一抽象硬件，让上层 `read/write` 不再认识具体设备。

## 新增文件（相对上一章 ch13）与新增能力
- 新增文件：
  - `src/driver/char_dev.c`
  - `include/char_dev.h`
  - `src/driver/uart_char.c`
  - `src/driver/ramdisk.c`
  - `include/block_dev.h`
- 新增能力（从书稿第十四章提取）：
  - **字符设备框架** `char_dev`：定义 `struct char_dev`（name/major/minor + open/close/read/write/ioctl 函数指针），`char_dev.c` 维护 `char_devs[MAX_CHAR_DEVS]` 设备表并提供注册/查找；`ioctl` 可置 NULL，框架分发前判空防御。
  - **块设备框架** `block_dev`：`struct block_dev` 以 512 字节块为单位读写（open/close/read/write/size），与字符设备字节流模型区分。
  - 注册两个具体设备：UART 字符设备 `ttyS0`（把第 4 章 PL011 裸寄存器驱动 `uart_putc/uart_getc` 包装成统一接口，major=4）与 ramdisk 块设备 `ram0`（内存模拟块设备，major=1）。
  - 分层观：裸寄存器驱动（ch4 最底层）→ 字符/块设备包装（本章中间层）→ 系统调用/shell（上层）。

## 编译与运行
```bash
cd .../code-by-chapter/ch14
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用
```
预期输出（沙箱无 QEMU，未实机验证；依据书稿与源码分析）：
```
Hello, MyOS!
Chapter 4: UART Hello World
```
说明：本快照 `main.c` 仍为第四章最小教学版，内核运行输出固定为上面两行。本章链接集新增 `char_dev.c`/`uart_char.c`/`ramdisk.c`（三者均可独立链接，`char_dev.o` 同时提供 `char_dev_register` 与 `block_dev_register`，已 `nm` 验证），但最小 `kernel_main` 未调用 `*_register` 初始化与设备分发路径；设备模型真正被 shell/系统调用驱动要到全符号齐备（ch18）后。

## 已引入但暂未链接/执行的模块
- 本章新增的三个 `.c` 均已链接，无新增暂不链接模块。
- 此前各章引入仍暂未链接/构建：`src/boot/exception.S`、`src/kernel/irq.c`、`src/proc/proc.c`、`src/driver/timer.c`、`src/kernel/syscall.c`、`src/kernel/semaphore.c`（互锁簇，ch18 全链接）；`user/net.c`（自 ch16 起作为用户程序构建）。

## 尚未包含（下一章起才出现）的模块
- 第 15 章：文件系统 `tmpfs.c`、`vfs.h`、`string.h`（内存文件系统 + VFS 统一接口）。
- 再往后：用户态 shell 与内嵌用户程序（ch16）、网络栈（ch18）。
