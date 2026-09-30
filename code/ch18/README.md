# ch18 网络协议栈 — 本章代码快照

## 本章主题
为内核接入网络：通过 PCIe ECAM 配置空间找到 virtio-net 网卡，实现现代 virtio PCI 驱动，搭起精简 IPv4 协议栈（以太网帧收发、ARP、IP 路由与校验、ICMP ping 应答、UDP socket），并把第 12 章的三个网络系统调用开放给用户程序（`run net` 真实解析 example.com 的 A 记录）。

**本章关键转折**：网络代码加入后，此前一直互锁、无法单独链接的中断/调度/系统调用簇（`exception.S`/`irq.c`/`proc.c`/`timer.c`/`syscall.c`/`semaphore.c`）全部可解析；`main.c` 也从最小版换成 `code/` 最终版。至此**全部源文件参与链接，得到完整可启动内核**。

## 新增文件（相对上一章）与新增能力
本章相对 ch17 新增 12 个文件：

- `src/kernel/pci.c`：PCIe ECAM 配置空间读写；新/旧双基址（0x4010000000 / 0x3f000000）探测，按 VID/DID（0x1af4/0x1000）找网卡，固定 BAR4 到 0x10000000。
- `src/kernel/virtio_net.c`：virtio-net 现代 PCI 驱动——能力链表取 Notify/DeviceCfg 偏移、状态机（RESET→ACK→DRIVER→FEATURES_OK→DRIVER_OK）、RX/TX 双 vring、RX 填充 + `virtio_net_poll` 轮询收包（含 QEMU DMA 偏移 ±10 补偿）。
- `src/kernel/net/net.c`：`net_init()` 与 `net_rx_handler()` 按以太网类型分发 ARP/IP。
- `src/kernel/net/arp.c`：16 项 ARP 缓存，请求/应答，`resolve_arp` + 未命中时的 ARP 等待循环。
- `src/kernel/net/ip.c`：IPv4 收包检查链（版本/IHL/校验和/目的地址）与发送（子网/网关路由、ARP 轮询等待）。
- `src/kernel/net/icmp.c`：ping 请求应答（改原缓冲、重算校验和）。
- `src/kernel/net/udp.c`：UDP socket 模型（socket/bind/sendto/recvfrom），校验和置 0。
- `src/kernel/net/checksum.c`：RFC 1071 互联网校验和。
- 头文件：`include/pci.h`、`include/virtio_net.h`、`include/net.h`、`include/udp.h`。

新增能力：
- **完整内核首次链接成功**：所有前序"已引入但暂不链接"的模块本章全部进入链接表。
- `net_init()` 在启动序列第 9 步运行，打印 `virtio-net: initialized, MAC ...` 与 `net: protocol stack initialized`。
- shell 里 `run net` 走完整条收发链路（ARP→UDP→slirp DNS→解析 A 记录）。

## 编译与运行
```bash
cd .../code-by-chapter/ch18
make CROSS=/home/user/Doubao/chats/38442996048129282/toolchain/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-
make run   # 若 QEMU 可用（本 Makefile 的 run 已含 -netdev user,id=net0 -device virtio-net-pci,netdev=net0）
```
本章 `Makefile` = `code/Makefile` 逐字拷贝（wildcard 收集全部源文件，含 `run` 的网络设备参数）。构建产物：`kernel.elf`/`kernel.bin` + `user/{shell,hello,test,net}.bin`。

预期输出（**未实机验证、基于书稿第十七章 §四/§五 记录与代码分析**；本沙箱无 QEMU）：
```
MyOS v0.3 booting...
Page allocator initialized: 00008000 total pages, 00007efb free pages
Process manager initialized
syscall table initialized
VFS (tmpfs) initialized
virtio-net: initialized, MAC 52:54:00:12:34:56
net: protocol stack initialized
embedded shell: 00007f20 bytes
Timer frequency: 03b9aca0 Hz
Timer initialized at 00000064 Hz
Boot complete. Starting scheduler...

MyOS Shell -- type 'help' for commands
$
```
进入 shell 后（书稿 §五）：
```
$ help
Commands:
  help          - show this help
  echo <text>   - print text
  ps            - list processes
  ls [path]     - list files
  mkdir <path>  - create directory
  cat <file>    - print file content
  run hello     - fork+exec hello program
  run test      - fork+exec test program
  run net       - UDP DNS query demo
  pid           - print shell pid
  clear         - clear screen
  exit          - quit shell
$ run hello
Hello from user space!
my pid = 2
working ... 1/3
working ... 2/3
working ... 3/3
Goodbye!
[child pid=2 exited with 0]
$ run test
Hello, User!
[child pid=3 exited with 0]
$ run net
[net] UDP DNS query demo (10.0.2.3:53)
[net] querying example.com -> 29 bytes sent
[net] example.com = 104.20.23.154
[net] done
[child pid=4 exited with 0]
```
（`example.com` 的解析结果为 Cloudflare 真实地址，多次运行因 DNS 轮询略有不同。）

## 已引入但暂未链接/执行的模块
**本章无。** 此前 ch16/ch17 一直保留但不链接的 `exception.S`、`irq.c`、`proc.c`、`timer.c`、`syscall.c`、`semaphore.c`，本章因网络符号补齐而全部进入链接表，与 `start.S`/`context_switch.S`/`embed.o`/最终版 `main.c` 共同构成完整内核。

## main.c 切换
本章 `src/kernel/main.c` 从最小教学版切换为 `code/` 最终版（逐字一致）：完整 11 步初始化序列（UART→异常/中断→页分配→slab→进程→系统调用→设备→VFS→网络→创建 shell→启动时钟）。至此全书 main.c 演进结束。

## 尚未包含（下一章起才出现）的模块
- 第 19 章为总结，无新增代码；文件集合 = `code/` 全量（另加入 `code/README.md`）。
