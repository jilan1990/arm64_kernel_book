# ch15 文件系统 — 本章代码快照

## 本章主题
实现一个内存文件系统（tmpfs）并在其上搭建 VFS 统一接口，把字节组织成"名字 → inode → 数据"的目录树。

## 新增文件（相对上一章 ch14）与新增能力
- 新增文件：
  - `src/fs/tmpfs.c`（tmpfs 实现 + VFS 公共接口）
  - `include/vfs.h`（VFS 数据结构与接口声明）
  - `include/string.h`（内核字符串工具）
- 新增能力（从书稿第十五章提取）：
  - **inode 树**：目录与文件都是 `struct inode`（ino/type/size/data/parent/children/next/name/refcount），目录经 `children` 链表挂子节点，构成目录树与兄弟链表。
  - **打开文件表**：`static struct file file_table[MAX_FILES]`（64 槽），打开文件占一个 `used=1` 槽位、返回 `&file_table[i]` 直接当 fd（"指针即 fd"模型），关闭清 `used`。
  - **VFS 方法表** `struct fs_operations`（lookup/create/unlink/read/write 五个函数指针）：VFS 层只调方法表不关心实现，为将来挂载第二个文件系统预留接口（Linux VFS 的最小形态）。
  - 提供 `vfs_init/vfs_open/vfs_close/vfs_read/vfs_write/vfs_mkdir/vfs_list/vfs_path_lookup`，可支撑 shell 的 `cat/ls/mkdir`；文件全在内存，不依赖真实磁盘即可演示文件语义。

## 编译与运行
```bash
cd .../code-by-chapter/ch15
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用
```
预期输出（沙箱无 QEMU，未实机验证；依据书稿与源码分析）：
```
Hello, MyOS!
Chapter 4: UART Hello World
```
说明：本快照 `main.c` 仍为第四章最小教学版，内核运行输出固定为上面两行。本章链接集新增 `tmpfs.c`（可独立链接，依赖的 slab/uart/string 均已在链接集），但最小 `kernel_main` 未调用 `vfs_init` 与 `vfs_*`；文件系统真正被 `sys_open/sys_read/...`（ch12 syscall.c，ch18 才链接）与 shell（ch16）驱动要到全符号齐备后。

## 已引入但暂未链接/执行的模块
- 本章新增 `tmpfs.c` 已链接，无新增暂不链接模块。
- 此前各章引入仍暂未链接/构建：`src/boot/exception.S`、`src/kernel/irq.c`、`src/proc/proc.c`、`src/driver/timer.c`、`src/kernel/syscall.c`、`src/kernel/semaphore.c`（互锁簇，ch18 全链接）；`user/net.c`（自 ch16 起作为用户程序构建）。
- 注意：`syscall.c`（ch12）引用的 `vfs_open/vfs_read/vfs_write/vfs_close/vfs_list/vfs_mkdir` 在本章已由 `tmpfs.o` 提供符号——但 `syscall.o` 本身仍因同时引用第 18 章 `udp_*/virtio_net_poll` 符号而暂不链接，两文件的链接耦合点在 ch18 一并解决。

## 尚未包含（下一章起才出现）的模块
- 第 16 章：用户态 shell 与内嵌用户程序 `user/shell.c`、`user/hello.c`、`user/test.S`、`user/user_link.ld`、`src/boot/embed.S`（本章引入的 `user/net.c` 自此章起可构建）。
- 再往后：集成实验（ch17）、网络栈与完整内核（ch18）、全书回顾（ch19）。
