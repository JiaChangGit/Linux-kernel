# Linux Kernel / Embedded Systems Portfolio 技術總報告

## 1. 報告定位

這份 `report.md` 以目前最上層目錄的**實際專案內容**為準，不只看舊版 `README.md`。  
依照目前 repository 狀態，這裡共有六個主要主題專案：

1. `fwsh`
2. `chardev-driver`
3. `qemu-platform-demo`
4. `isr_dma_demo`
5. `cpu-scheduling-qemu`
6. `linux-ipc-benchmark`

如果要用一句話總結這個 repository：

> 這是一組從 user space、kernel module、IPC、platform driver、QEMU 驗證，到 trace / observability 設計都涵蓋到的 Linux 系統軟體作品集。

本報告的目標有三個：

1. 快速說清楚每個子專案在做什麼。
2. 深入拆解關鍵技術與其工程意義。
3. 特別把 `Trace` / `Tracing` / `Observability` 如何被實作出來講清楚，而且用字精確，不把一般 log 混稱成完整 tracing framework。

---

## 2. 先看懂全貌

### 2.1 專案總覽

| 專案 | 層次 | 主題 | 最重要的技術 |
|---|---|---|---|
| `fwsh` | User Space | Shell 與程序控制 | `fork()`、`execvp()`、`pipe()`、`dup2()`、Readline、dispatch table |
| `chardev-driver` | Kernel Space | Character Device Driver | `cdev`、`file_operations`、`ioctl`、`procfs`、`sysfs`、`mutex` |
| `qemu-platform-demo` | Embedded Linux / ARM64 | Platform Driver bring-up | Device Tree、`platform_driver`、MMIO、`devm_ioremap_resource()`、initramfs、QEMU |
| `isr_dma_demo` | Kernel + Userspace Data Path | ISR-like producer + DMA ring buffer | `hrtimer`、`dma_alloc_coherent()`、`mmap()`、ring buffer、`/proc`、memory barrier |
| `cpu-scheduling-qemu` | Simulator + VM Automation | CPU scheduling 演算法與 execution trace | FCFS / SJF / SRTF / RR、`GanttSlot`、structured benchmark output、QEMU VM |
| `linux-ipc-benchmark` | Kernel + Userspace IPC | Message Queue vs Shared Memory | `kfifo`、wait queue、`vmalloc()`、`mmap()`、`atomic64_t`、`pthreads` benchmark |

### 2.2 這個 repository 真正在展示什麼能力

這不是單一題目，而是一條很清楚的能力鏈：

1. `fwsh` 先處理 user space 的 process、pipe、file descriptor、command dispatch。
2. `chardev-driver` 把 Linux 一切皆檔案（everything is a file）的 VFS 抽象往 kernel space 延伸。
3. `qemu-platform-demo` 進一步處理 embedded Linux 典型的 Device Tree、platform driver、initramfs、ARM64/QEMU bring-up。
4. `isr_dma_demo` 把問題提升到高頻資料路徑、ring buffer、共享記憶體與 zero-copy。
5. `cpu-scheduling-qemu` 則把「狀態轉移、時間推進、可重現 trace」抽象成排程模擬器。
6. `linux-ipc-benchmark` 用兩種 kernel module 親自對照 IPC 路徑成本，讓「shared memory 比 message queue 快」不只是口號，而是可讀、可測、可驗證的程式路徑差異。

---

## 3. 技術地圖

### 3.1 User Space（使用者空間）

代表專案：

- `fwsh`
- `cpu-scheduling-qemu/src/scheduler.c`
- `linux-ipc-benchmark/user/*`
- `isr_dma_demo/userspace/consumer.c`

核心主題：

- POSIX process control
- I/O redirection
- pipeline
- `pthread` 併發
- `mmap()` 使用者端共享記憶體存取
- benchmark 與 structured output

### 3.2 Kernel Space（核心空間）

代表專案：

- `chardev-driver/driver/chardev.c`
- `isr_dma_demo/kernel/isr_dma_module.c`
- `linux-ipc-benchmark/kernel/mq_module.c`
- `linux-ipc-benchmark/kernel/shm_module.c`

核心主題：

- `struct file_operations`
- `copy_to_user()` / `copy_from_user()`
- `procfs` / `seq_file`
- `sysfs`
- `ioctl`
- `mutex` / `spinlock` / wait queue / `atomic_t`
- `kfifo`
- `hrtimer`
- `dma_alloc_coherent()`
- memory mapping 與 page remap

### 3.3 Embedded Platform（嵌入式平台）

代表專案：

- `qemu-platform-demo`

核心主題：

- ARM64 cross-compilation
- Device Tree（裝置樹）
- DT overlay / DTB patching
- platform device / platform driver
- MMIO（Memory-Mapped I/O）
- BusyBox rootfs
- initramfs
- QEMU `virt` machine

### 3.4 Observability / Trace（可觀測性 / 追蹤）

這是目前整個 repository 最值得細看的共通能力。

重點不是只會印 `printf()` 或 `pr_info()`，而是會設計多層次的觀測面：

- lifecycle trace：系統走到哪一步
- state trace：系統目前狀態是什麼
- data trace：資料內容長什麼樣
- execution trace：執行順序與時間線
- performance trace：每條資料路徑的成本差異

---

## 4. 關鍵字中英文對照

| 中文 | English | 精確說明 |
|---|---|---|
| 行程 | Process | OS 管理的執行實體，擁有獨立虛擬位址空間 |
| 執行緒 | Thread | 同一行程內共享位址空間的執行單位 |
| 檔案描述符 | File Descriptor, FD | Linux 以整數代表開啟資源的方式，`stdin=0`、`stdout=1` |
| 虛擬檔案系統 | Virtual File System, VFS | 將一般檔案、裝置、`procfs`、`sysfs` 統一在檔案介面下 |
| 字元裝置 | Character Device | 以串流方式 `read/write` 的裝置介面，常見於 `/dev/*` |
| 程式間通訊 | Inter-Process Communication, IPC | 不同行程交換資料的方法 |
| 共享記憶體 | Shared Memory | 多個執行實體看到同一份資料頁面 |
| 零拷貝 | Zero-Copy | 傳遞過程不需要額外 `copy_from_user()` / `copy_to_user()` |
| 環形緩衝區 | Ring Buffer / Circular Buffer | 用 `head` / `tail` 管理固定容量資料區 |
| 互斥鎖 | Mutex | 可睡眠鎖，適合 process context 的互斥 |
| 自旋鎖 | Spinlock | 忙等鎖，不可在持鎖期間睡眠 |
| 等待佇列 | Wait Queue | 條件未成立時讓執行緒睡眠，成立後再喚醒 |
| 原子變數 | Atomic Variable, `atomic_t` / `atomic64_t` | 適合簡單計數與 lock-free 狀態欄位 |
| 記憶體屏障 | Memory Barrier | 保證讀寫可見順序，不讓 CPU / compiler 任意重排 |
| 裝置樹 | Device Tree, DT | 用資料結構描述硬體，而不是把硬體資訊硬寫在 driver C code 裡 |
| 平台驅動 | Platform Driver | 常見於 SoC / embedded Linux 的 driver 類型，透過 Device Tree 配對 |
| 記憶體映射 I/O | Memory-Mapped I/O, MMIO | 用記憶體位址存取硬體暫存器 |
| 追蹤 | Trace / Tracing | 對事件、狀態、時間或資料內容進行可重建的記錄 |
| 可觀測性 | Observability | 系統是否能被測量、定位、解釋問題 |

---

## 5. 各子專案深入分析

## 5.1 `fwsh`：從 Shell 看懂 POSIX 系統程式

### 5.1.1 它不是「只會跑 command」的玩具

`fwsh` 的重點不是把 `system()` 包起來，而是自己實作 shell 核心骨架：

- parser
- executor
- REPL
- built-in command dispatch
- signal handling

### 5.1.2 Parser（語法解析器）怎麼做

`fwsh/src/parser.c` 採用「簡單 lexer + 遞增建構」模式，把命令列解析成 `Pipeline` 與 `Cmd`。

它能處理：

- 一般 token
- `|`
- `<`
- `>`
- `>>`
- `&`
- 單引號與雙引號

例如：

```bash
cat firmware.bin | hexdump 0x40 > dump.txt &
```

會被解析成：

- `ncmds = 2`
- `background = 1`
- 第 1 段：`cat firmware.bin`
- 第 2 段：`hexdump 0x40 > dump.txt`

這個設計很重要，因為 shell 真正困難的地方不在執行，而在於先把「文字」準確轉成「結構化語意」。

### 5.1.3 Executor（執行器）怎麼把 pipeline 接起來

`fwsh/src/executor.c` 的技術核心是：

- `pipe()`
- `fork()`
- `dup2()`
- `execvp()`
- `waitpid()`

對 `A | B | C` 而言，shell 必須先建立兩組 pipe，再讓：

- `A` 的 `stdout` 指向第一組 pipe 的 write end
- `B` 的 `stdin` 指向第一組 pipe 的 read end
- `B` 的 `stdout` 指向第二組 pipe 的 write end
- `C` 的 `stdin` 指向第二組 pipe 的 read end

這件事不是抽象概念，而是實際的 file descriptor 重新接線。  
若 parent 或 child 沒有正確關閉不再使用的 pipe fd，讀端永遠等不到 EOF，pipeline 就會卡住。這是很多初學者 shell 會犯的典型錯誤。

### 5.1.4 Built-in command 為何用 dispatch table

`fwsh/src/builtin.c` 用函式指標表（function pointer dispatch table）管理 built-in command：

- `cd`
- `pwd`
- `history`
- `exit`
- `hexdump`
- `crc32`
- `memmap`

這種設計的好處是擴充性非常清楚：

1. 實作新函式。
2. 在表格中新增一筆。

不需要改一長串 `if/else`。  
這種 pattern 在 firmware command table、bootloader monitor、CLI-based embedded diagnostic tool 中非常常見。

### 5.1.5 這個專案的工程價值

`fwsh` 真正訓練的是：

- process lifecycle 思維
- fd ownership 思維
- 文字命令到結構化資料的解析能力
- 可擴充命令框架設計

這些能力後續會直接延伸到 driver 介面設計與 trace 工具設計。

---

## 5.2 `chardev-driver`：VFS、`ioctl`、`procfs`、`sysfs` 的入門完整體

### 5.2.1 這個 driver 具備哪些介面

`chardev-driver/driver/chardev.c` 同時實作：

- `/dev/chardev0`
- `/proc/chardev_info`
- `/sys/class/chardev/chardev0/*`
- `ioctl`

這表示同一個 driver 同時提供：

- data path
- control path
- status path

這是非常完整的教學型設計。

### 5.2.2 `file_operations` 是 user-kernel 介面的核心

這個專案透過 `struct file_operations` 把 userspace 的檔案操作對應到 driver 函式：

- `.open`
- `.read`
- `.write`
- `.unlocked_ioctl`
- `.release`

所以：

```bash
echo "Firmware Engineer" > /dev/chardev0
cat /dev/chardev0
```

背後其實是：

1. shell 發出 `write()`
2. VFS 導向 `chardev_write()`
3. driver 用 `copy_from_user()` 把資料放進 kernel buffer
4. `cat` 呼叫 `read()`
5. VFS 導向 `chardev_read()`
6. driver 用 `copy_to_user()` 把資料傳回 userspace

### 5.2.3 為什麼要同時做 `procfs` 和 `sysfs`

兩者用途不同：

- `procfs` 偏向狀態報告與偵錯資訊
- `sysfs` 偏向裝置屬性與控制介面

在這個專案中：

- `/proc/chardev_info` 會列出 buffer 狀態與計數器
- `sysfs` 的 `read_only` 可以直接切換唯讀模式
- `sysfs` 的 `stats` 與 `buf_len` 讓狀態更容易被腳本讀取

這種拆法很合理，因為它把「觀察」與「控制」分開了。

### 5.2.4 `ioctl` 的角色

這個 driver 的 `ioctl` 用於做不適合單純 `read/write` 的控制：

- reset buffer
- get length
- set read-only

`ioctl`（Input/Output Control）常被濫用，但這個專案的使用方式是合理的：  
它把「非串流資料交換，而是命令型控制」的需求清楚獨立出來。

### 5.2.5 這個專案的教學重點

如果要學 Linux driver，這個專案很適合作為第一個完整範例，因為它一次把下面這些概念串起來：

- `alloc_chrdev_region()`
- `cdev_add()`
- `class_create()`
- `device_create()`
- `copy_to_user()` / `copy_from_user()`
- `proc_create()` + `seq_file`
- sysfs attribute
- `ioctl`
- `mutex`

---

## 5.3 `qemu-platform-demo`：Embedded Linux bring-up 的標準路徑

### 5.3.1 這個專案回答的不是「怎麼寫一個 driver」而已

它回答的是更完整的 embedded Linux 問題：

1. ARM64 kernel 怎麼建？
2. Device Tree 怎麼補 node？
3. out-of-tree platform driver 怎麼編？
4. rootfs / initramfs 怎麼組？
5. QEMU 怎麼把整個系統跑起來？

### 5.3.2 Device Tree 與 `compatible`

`qemu-platform-demo/dts/myled-fragment.dts` 新增了一個虛擬 LED controller：

```dts
compatible = "myvendor,myled-v1";
reg = <0x0 0x10010000 0x0 0x1000>;
num-leds = <4>;
default-brightness = <180>;
```

這幾個欄位的工程意義很明確：

- `compatible`：決定哪個 driver 可以 match
- `reg`：MMIO 位址與大小
- `num-leds`：driver 可讀取的自訂屬性
- `default-brightness`：初始化行為

### 5.3.3 Platform Driver 的 `probe()` 如何被觸發

`qemu-platform-demo/driver/myled_ctrl.c` 定義 OF match table：

```c
{ .compatible = "myvendor,myled-v1" }
```

kernel 開機解析 DTB 後，若找到相容節點，便建立 platform device，接著呼叫 driver 的 `probe()`。

`probe()` 內部做的事情包含：

- 解析 Device Tree property
- 取得 MMIO resource
- `devm_ioremap_resource()` 映射暫存器區
- 初始化 driver private data
- 建立 sysfs attribute group

### 5.3.4 為什麼這個 driver 有 simulated mode

`myled_ctrl.c` 很值得注意的一點是：若硬體 MMIO 區沒有真實回應，它會退回 simulated mode，用 shadow register array 模擬暫存器。

這不是偷懶，而是很實際的 demo engineering：

- 可以在 QEMU 上穩定跑
- 可以保留 register-model 與 sysfs 控制流程
- 可以把重點放在 platform driver lifecycle，而不是卡在不存在的真實硬體

### 5.3.5 這個專案最重要的價值

很多人會寫 module，但不熟完整 bring-up 流程。  
這個專案的價值在於它展示了：

- kernel
- DTB
- driver
- rootfs
- QEMU

這五者如何在同一條路徑上整合。

---

## 5.4 `isr_dma_demo`：高頻資料路徑、DMA ring buffer 與多層 trace

### 5.4.1 這是本 repository 最值得深挖的 kernel data-path 專案之一

`isr_dma_demo/kernel/isr_dma_module.c` 做的事情非常具體：

1. 用 `hrtimer` 模擬高頻 ISR-like producer。
2. 每 500 us 產生一筆固定大小資料。
3. 寫入共享 ring buffer。
4. userspace 透過 `read()` 或 `mmap()` 嘗試消費。
5. 用 `/proc`、`dmesg`、hexdump、benchmark 組成完整 observability surface。

### 5.4.2 Ring buffer layout 很清楚

共享記憶體前半部放 control block：

- `head`
- `tail`
- `slots`
- `slot_size`
- `isr_count`
- `drop_count`

後半部放實際 payload。

每個 slot 64 bytes：

- 前 8 bytes：timestamp
- 接著 8 bytes：ISR counter
- 剩餘 48 bytes：`0xAB` pattern fill

這個設計的優點是資料內容可被直接驗證，不只是「看起來有資料」。

### 5.4.3 為什麼要用 `dma_alloc_coherent()`

這個專案想展示的是 DMA-coherent shared buffer 的概念。  
若配置成功：

- kernel 與 potential DMA device 對這塊記憶體有一致觀點
- 不需要手動 cache flush

若平台條件不允許，專案會 fallback 到 `vzalloc()`。  
這一點也寫得很誠實：fallback 可以維持 demo 可執行，但語義上不再是「真正 DMA-coherent 配置」。

### 5.4.4 `mmap()` 與 `read()` 是兩條不同成本路徑

- `read()`：每次取一個 slot，kernel 需要 `copy_to_user()`
- `mmap()`：使用者先映射整塊共享區，之後直接從共享頁面讀

這正是 zero-copy 教學的核心。

### 5.4.5 目前程式狀態必須講精確

依目前 repository 內容：

- module load 正常
- `/dev/isr_dma` 正常
- `/proc/isr_dma_stats` 正常
- `read()` 路徑可工作
- `mmap()` benchmark path 的程式碼存在
- 但目前 userspace mapping 大小與 kernel 匯出大小不一致，因此 `mmap()` 目前會回 `EINVAL`

這表示此專案**非常有教學價值**，但 `mmap end-to-end benchmark` 目前不是完成狀態。這一點在技術報告裡必須講清楚，不能模糊帶過。

---

## 5.5 `cpu-scheduling-qemu`：不是 kernel scheduler，而是可觀測的 scheduling simulator

### 5.5.1 先澄清定位

這個專案不是修改 Linux kernel scheduler。  
它也不是用 `ftrace`、`perf`、`eBPF` 去抓真實核心排程事件。

它是：

- 用 C 實作 FCFS / SJF / SRTF / Priority / RR
- 用固定 workload 驗證
- 用 QEMU VM 建出可重現執行環境
- 用 `GanttSlot` 與 `BENCHMARK` 行輸出 execution trace 與 benchmark telemetry

### 5.5.2 `gantt_push()` 是這個專案的靈魂

`cpu-scheduling-qemu/src/scheduler.c` 中，`gantt_push()` 不只是 append，而是做 trace 壓縮：

- 如果上一個區段與目前 PID 相同，直接延長 end time
- 否則新增新的 `GanttSlot`

這使得逐時間單位的 SRTF 決策，不會輸出一堆難讀的碎片。

例如連續四個 tick 都是 `P2`：

```text
(P2,1,2) (P2,2,3) (P2,3,4) (P2,4,5)
```

會被壓縮成：

```text
(P2,1,5)
```

這是很標準、很實用的 trace compression 思維。

### 5.5.3 為什麼 SRTF 最能看出 trace 的價值

因為 SRTF 會 preempt。  
只看最後平均值，你不知道中間何時切換；  
有 `GanttSlot`，你才真的看得到：

- 哪個工作被打斷
- 新工作在哪個時間點插入
- preemption 是不是符合演算法定義

### 5.5.4 `BENCHMARK ...` 行是結構化 trace

`print_results()` 會輸出：

```text
BENCHMARK <label> AWT=<...> ATT=<...> ART=<...>
```

這不是裝飾字串，而是 machine-readable interface。  
`scripts/04_benchmark.sh` 會抓這些行，再轉成 `results/benchmark.csv`。

也就是：

```text
scheduler.c
  -> BENCHMARK line
  -> shell script parser
  -> CSV
  -> benchmark report
```

這是一條完整的 observability data pipeline。

---

## 5.6 `linux-ipc-benchmark`：把 IPC 路徑成本直接攤開來看

### 5.6.1 這個專案不是呼叫現成 POSIX IPC API 而已

它是自己寫兩個 kernel module：

- `mq_module.ko`
- `shm_module.ko`

分別代表：

- Message Queue 路徑
- Shared Memory 路徑

### 5.6.2 為什麼這個對照有價值

這個專案最聰明的地方不是「benchmark 了兩種 IPC」，而是把對照拆成三條路徑：

1. MQ syscall path
2. SHM syscall path
3. SHM `mmap()` zero-copy path

這樣就能把問題拆開：

- queue 機制差異
- copy 次數差異
- syscall 次數差異

### 5.6.3 `mq_module.c` 的教學重點

`mq_module.c` 以 `kfifo` 為核心，搭配：

- `mutex`
- `wait_event_interruptible()`
- `wake_up_interruptible()`

這條路徑的成本非常清楚：

```text
write()
  -> copy_from_user()
  -> kfifo_in()

read()
  -> kfifo_out()
  -> copy_to_user()
```

也就是每筆訊息穿越 user/kernel 邊界兩次。

### 5.6.4 `shm_module.c` 的教學重點

`shm_module.c` 內部以 `vmalloc()` 配置 ring buffer，並提供兩種用法：

- syscall path：仍用 `write()` / `read()`，因此仍有 copy
- `mmap()` path：把同一份頁面映射到 userspace，之後直接讀寫共享區

`mmap()` 實作時，它逐頁呼叫：

- `vmalloc_to_pfn()`
- `remap_pfn_range()`

這是因為 `vmalloc()` 出來的頁面在虛擬位址上連續，但在實體位址上不保證連續。

### 5.6.5 這個專案為何也很適合拿來談 trace

它不是完整 tracing framework，但它有相當清楚的觀測面：

- `/proc/mq_stats`
- `/proc/shm_stats`
- kernel-side average latency counters
- userspace producer/consumer/wall-clock benchmark

也就是它能同時觀察：

- queue 有沒有動
- latency 有沒有累積
- throughput 有沒有差

---

## 6. 跨專案技術深入探討

## 6.1 VFS（Virtual File System）為什麼是 driver 的入口

Linux 的強大之處之一，是把很多資源都抽象成檔案。  
因此：

- 一個 shell 會用 `open/read/write`
- 一個字元裝置 driver 也會透過 `open/read/write`
- `/proc` 與 `/sys` 雖然不是實體磁碟檔案，也走檔案語意

`chardev-driver`、`isr_dma_demo`、`linux-ipc-benchmark` 都是靠這套抽象讓 userspace 跟 kernel 溝通。

這樣的優點是：

1. userspace 介面統一
2. 工具相容性高
3. 除錯容易

例如 `cat /proc/...`、`echo 1 > /sys/...` 之所以成立，就是因為 VFS 幫你把不同背後機制統一了。

## 6.2 `procfs`、`sysfs`、`ioctl` 要怎麼分工

這三者常被混用，但其實角色應該分清楚。

### `procfs`

適合：

- 統計
- 調試資訊
- runtime 狀態快照

本 repo 例子：

- `/proc/chardev_info`
- `/proc/isr_dma_stats`
- `/proc/mq_stats`
- `/proc/shm_stats`

### `sysfs`

適合：

- 裝置屬性
- 單一欄位式控制
- 自動化腳本容易存取的設定點

本 repo 例子：

- `chardev` 的 `read_only`
- `myled` 的 `enable`、`brightness`、`color`

### `ioctl`

適合：

- 不容易用純文字欄位表達的控制命令
- 有結構化參數或命令碼的操作

本 repo 例子：

- `chardev` reset / get length / set read-only
- `isr_dma` reset stats / query slot size

## 6.3 同步機制選擇不是隨便的

這個 repository 很適合拿來比較 Linux 常見同步原語。

### `mutex`

出現在：

- `chardev-driver`
- `mq_module.c`

適合原因：

- process context
- 可以睡眠
- 臨界區內容不是極短的硬中斷級路徑

### `spinlock`

出現在：

- `isr_dma_demo` 的 naive path
- `shm_module.c` 的 syscall ring path
- `myled_ctrl.c` 的 register access

適合原因：

- 臨界區短
- 不可睡眠
- 有些情境接近 IRQ / MMIO 共享保護

### wait queue

出現在：

- `mq_module.c`

適合原因：

- queue full / empty 時不該 busy loop 浪費 CPU
- producer / consumer 關係很明確

### `atomic_t` / `atomic64_t`

出現在：

- `isr_dma_demo`
- `linux-ipc-benchmark`
- `chardev-driver` 的 counters

適合原因：

- 簡單計數
- head/tail 之類輕量欄位
- 不想為了單純 counter 引入整把鎖

## 6.4 Ring Buffer（環形緩衝區）為什麼到處出現

在這個 repository 裡，ring buffer 不是偶然，而是核心主題之一。

出現位置：

- `isr_dma_demo`
- `linux-ipc-benchmark/shm_module.c`
- `mq_module.c` 內部的 `kfifo`

原因很簡單：  
它非常適合串流資料或 producer-consumer 模型。

基本規則：

- Empty：`head == tail`
- Full：`next(head) == tail`

這種結構的好處是：

- 固定容量
- O(1) push / pop
- 不需要搬移整塊資料

但真正困難的地方不在數學，而在：

- 多執行實體同步
- overflow policy
- memory ordering

## 6.5 Memory Barrier（記憶體屏障）不能只背名字

這個 repo 裡，`isr_dma_demo` 與 `linux-ipc-benchmark` 都讓 memory ordering 問題變得很具體。

典型原則是：

1. 先把 payload 寫完。
2. 再更新 `head`。
3. consumer 先看到新的 `head` 時，必須保證 payload 已經可見。

因此你會看到：

- `smp_store_release()`
- `smp_wmb()`
- `smp_rmb()`
- `__sync_synchronize()`

它們不是為了炫技，而是為了避免出現這種錯誤：

> consumer 已看到 head 前進，但該 slot 的資料其實還沒完整寫好。

這種 bug 最難抓，因為它通常不是每次都出現，而且常只在特定 CPU、特定負載下出現。

## 6.6 `mmap()` 與 zero-copy 為什麼重要

`mmap()` 的核心不是「把檔案映射進來很方便」，而是：

- 一次建立映射
- 之後避免每筆資料都進入 kernel
- 避免每筆資料都做 `copy_to_user()` / `copy_from_user()`

本 repo 的兩個代表：

- `isr_dma_demo`
- `linux-ipc-benchmark`

尤其 `linux-ipc-benchmark` 的設計很好，因為它把三種路徑排在一起比較：

1. MQ syscall
2. SHM syscall
3. SHM `mmap`

這樣就很容易看出真正的差異來自哪裡。

## 6.7 Device Tree 與 Platform Driver 是 embedded Linux 基本功

在 MCU 世界，很多設定可能直接寫死。  
但在 Linux/SoC 世界，driver 不應硬寫板子資訊，而是透過 Device Tree 取得：

- base address
- IRQ
- compatible
- property

這樣 driver 才能保持可攜與可重用。

`qemu-platform-demo` 把這件事做得很清楚：

- DTS fragment 描述硬體
- driver 用 `of_match_table` 配對
- `probe()` 再去取資源與初始化

這就是 Linux platform driver 的標準工作流。

---

## 7. `Trace` 專章：這個 repository 最值得細講的能力

## 7.1 先定義：這裡說的 `Trace` 是什麼

很多人一看到 `Trace`，會先想到：

- `ftrace`
- tracepoint
- `perf`
- `eBPF`
- LTTng

這些都是正規、成熟、框架化的 tracing 系統。  
但本 repository 大多數專案**沒有直接實作這些框架**。

因此本報告必須用精確語言：

> 這個 repository 的 trace 能力，主要是自行設計的 observability surface，以及少量的 execution trace 結構，而不是完整 kernel tracing subsystem。

換句話說，這裡的 `Trace` 可以分成五種層次。

## 7.2 第一層：Lifecycle Trace（生命週期追蹤）

實作方式：

- `pr_info()`
- `pr_warn()`
- `dev_info()`
- `dev_warn()`
- `dmesg`

出現專案：

- `chardev-driver`
- `qemu-platform-demo`
- `isr_dma_demo`
- `linux-ipc-benchmark`

它回答的問題是：

- 模組有沒有載入成功
- `probe()` 有沒有被呼叫
- timer 有沒有啟動
- `mmap()` 有沒有成功
- remove / unload 有沒有走完

這類 trace 不會記每一筆資料，但它提供**時間線骨架**。

### 例子

`isr_dma_demo` 會在以下節點印訊息：

- module loading
- timer started
- timer stopped
- reset
- `mmap` success
- module unloaded

這使得你即使不看 source，也能先從 `dmesg` 判斷「流程到底走到哪」。

## 7.3 第二層：State Trace（狀態追蹤）

實作方式：

- `proc_create()`
- `single_open()`
- `seq_read`
- `seq_printf()`

出現專案：

- `chardev-driver`
- `isr_dma_demo`
- `linux-ipc-benchmark`

這一層的特色是：  
它不是只記事件，而是把**當下狀態**穩定輸出。

### 最典型的例子：`/proc/isr_dma_stats`

它輸出：

- `isr_count`
- `drop_count`
- `ring_head`
- `ring_tail`
- `isr_dma_avg_ns`
- `naive_avg_ns`

這些欄位不是擺好看的。它們分別回答：

- producer 有沒有真的跑
- consumer 跟不跟得上
- ring buffer 現在是不是滿了
- 兩種核心路徑的平均成本差多少

### `seq_file` 為什麼值得學

自己手刻 `/proc` read handler 容易踩到：

- partial read
- offset handling
- repeated read

`seq_file` 把這些底層細節處理掉，讓開發者專注在「輸出什麼狀態」。

## 7.4 第三層：Data Trace（資料追蹤）

實作方式：

- `read()` 取出資料
- hexdump 顯示 payload
- ring slot 內容驗證

這一層最容易被忽略，但其實非常重要。

因為只有 counter，你只知道「資料數量」；  
看得到 payload，你才知道「資料本身正不正確」。

### 例子：`isr_dma_demo`

slot 的格式是：

- 8 bytes timestamp
- 8 bytes counter
- 48 bytes pattern

用 `hexdump -C` 看資料時，可以確認：

1. slot 邊界正確
2. timestamp 真的有更新
3. counter 真的有成長
4. payload 沒被破壞

這種 trace 對 driver bring-up 非常重要，因為許多 bug 不是「沒有資料」，而是「資料結構錯位」或「欄位值異常」。

## 7.5 第四層：Execution Trace（執行軌跡）

這一層在 `cpu-scheduling-qemu` 最明顯。

它不是觀察 kernel module，而是觀察演算法執行順序。

實作方式：

- `GanttSlot`
- `gantt_push()`
- `print_gantt()`

### 為什麼這算 trace

因為它保留了：

- 誰先執行
- 執行多久
- 何時切換
- 哪些片段被 preempt

### `gantt_push()` 為什麼關鍵

這個函式同時扮演：

- event append
- trace compression

如果沒有合併連續 PID，你會得到大量碎片；  
有合併後，trace 既保留真實執行順序，又能被人讀懂。

這種做法在真正系統裡也很常見：  
原始事件流可能很碎，但展示層會做壓縮與歸併。

## 7.6 第五層：Performance Trace（效能追蹤）

實作方式：

- `ktime_get()`
- `clock_gettime(CLOCK_MONOTONIC)`
- atomic counters
- structured benchmark line / key-value output / CSV

出現專案：

- `isr_dma_demo`
- `cpu-scheduling-qemu`
- `linux-ipc-benchmark`

這一層回答的問題是：

- 哪條路徑快
- 快多少
- 成本主要在 kernel 還是 userspace

### 例子 1：`isr_dma_demo`

kernel 內部用 `ktime_get()` 量：

- `isr_produce()` 平均成本
- `naive_produce()` 平均成本

再透過 `/proc` 導出。

### 例子 2：`cpu-scheduling-qemu`

`BENCHMARK ...` 行輸出：

- `AWT`
- `ATT`
- `ART`

shell script 再把它轉成 CSV。  
這就是典型的 structured telemetry。

### 例子 3：`linux-ipc-benchmark`

同時量：

- producer thread elapsed time
- consumer thread elapsed time
- wall-clock throughput
- kernel-side average latency

這使得你不只知道「結果快」，還知道「快在哪一層」。

---

## 8. `Trace` 是如何被實作出來的：三個代表案例

## 8.1 案例一：`isr_dma_demo` 的四層 trace 組合

這個專案可以說是本 repository 裡最完整的 trace 教學範例。

### 第一步：事件來源建立

`hrtimer` 每 500 us 觸發一次 callback。  
這不是實體 IRQ，但它在教學上扮演 ISR-like producer。

### 第二步：資料進入 ring buffer

`isr_produce()` 會：

1. 讀 `head` / `tail`
2. 檢查是否 full
3. 寫入 timestamp 與 counter
4. `smp_store_release()` 更新 `head`

這一步把**事件**變成**資料**。

### 第三步：狀態被統計

driver 同步維護：

- `isr_count`
- `drop_count`
- benchmark counters

這一步把資料路徑轉成可量化 state。

### 第四步：狀態被匯出

`/proc/isr_dma_stats` 透過 `seq_printf()` 輸出欄位。

這一步把 kernel 內部狀態變成 userspace 可觀察介面。

### 第五步：資料內容被驗證

userspace 可 `read()` 一個 slot，再用 hexdump 檢查 payload。

這一步把「有資料」進一步提升成「資料內容也對」。

### 第六步：效能結果被記錄

benchmark 腳本與 userspace consumer 會把結果寫成結果檔。

這一步把 trace 從人眼檢查提升成可持續比較的紀錄。

### 為什麼這很重要

很多系統只有 log；  
有些只有 counter；  
少數才會連 payload 與 benchmark 都留下。

`isr_dma_demo` 的價值就在於它把：

- lifecycle
- state
- data
- performance

四層 trace 一次串齊。

## 8.2 案例二：`cpu-scheduling-qemu` 的 execution trace

這個專案沒有 kernel module，卻非常適合拿來教 trace 的另一個面向：  
**不是觀察裝置，而是觀察決策過程。**

### 核心結構

- `Process`
- `GanttSlot`
- `gantt[]`

### trace 產生點

- FCFS / SJF / Priority：每選中一個 process，就記一段區間
- SRTF：每個 tick 都可能記一段
- RR：每個 quantum 記一段

### 展示層

`print_gantt()` 把區段轉成文字時間線。  
這讓人可以在沒有圖形化工具的情況下，直接看懂排程行為。

### 結構化輸出層

`BENCHMARK` 行再把結果轉成 scripts 可解析格式。

所以它的 trace 架構可以概括成：

```text
algorithm step
  -> gantt slot
  -> textual timeline
  -> BENCHMARK structured line
  -> CSV
```

這種設計很像 firmware 世界常見的：

- event buffer
- debug dump
- CI telemetry

## 8.3 案例三：`linux-ipc-benchmark` 的機制對照型 trace

這個專案的 trace 重點不是完整重建每筆訊息旅程，而是建立**足以比較機制成本**的觀測面。

### MQ module

用：

- `atomic64_t st_enq`
- `atomic64_t st_deq`
- `atomic64_t st_lat_ns_total`

再透過 `/proc/mq_stats` 導出。

### SHM module

同樣用：

- write / read count
- average latency
- ring used / free slots

再透過 `/proc/shm_stats` 導出。

### userspace benchmark

`benchmark.c` 同步量：

- producer thread 時間
- consumer thread 時間
- wall 時間
- throughput

這種 trace 雖然不是每筆 event log，但它足夠回答最重要的工程問題：

- 是 queue 機制慢，還是 copy 慢
- 是 syscall 成本高，還是同步成本高
- `mmap` 之後吞吐量是否明顯提升

---

## 9. 從這些專案抽出的工程原則

## 9.1 Trace 不等於一堆 log

這是本報告最重要的觀念之一。

真正有價值的 trace，至少要能回答其中幾個問題：

1. 事件發生了沒有
2. 當下狀態是什麼
3. 資料內容正不正確
4. 執行順序是什麼
5. 哪條路徑成本更高

只會印 `printf("here\\n")` 不算完整 trace 設計。

## 9.2 好的觀測面要分層

本 repository 多個專案共同證明一件事：

- 只有 lifecycle log，不夠
- 只有 counter，不夠
- 只有 payload dump，不夠
- 只有 benchmark summary，也不夠

至少要把不同層次分清楚，才能真正 debug。

## 9.3 好的 benchmark 需要結構化輸出

`cpu-scheduling-qemu` 的 `BENCHMARK` 行與 `linux-ipc-benchmark` 的 summary，都反映同一個原則：

> 人類可讀輸出很重要，但機器可解析輸出同樣重要。

因為只要結果能被穩定解析，就能：

- 自動彙整
- 轉 CSV
- 做多次比較
- 接 CI

## 9.4 Honest reporting 比空泛宣稱更重要

這個 repository 有幾個地方特別值得肯定，因為它沒有模糊帶過限制：

- `isr_dma_demo` 明確說出目前 `mmap()` benchmark path 尚未完成
- `cpu-scheduling-qemu` 明確說出它是 simulator，不是真實 kernel scheduler trace
- `qemu-platform-demo` 的 simulated mode 明確標示 fallback 行為

技術報告最怕的不是功能不完整，而是把未完成狀態包裝成已完成。  
這個 repository 的整體方向是誠實的，這是優點。

---

## 10. 建議閱讀順序

如果是第一次讀這個 repository，建議順序如下：

1. `fwsh`
2. `chardev-driver`
3. `linux-ipc-benchmark`
4. `isr_dma_demo`
5. `qemu-platform-demo`
6. `cpu-scheduling-qemu`

原因：

- 前兩個先建立 user/kernel 介面基本功
- `linux-ipc-benchmark` 與 `isr_dma_demo` 進入共享記憶體、ring buffer、trace
- `qemu-platform-demo` 進入 embedded Linux bring-up
- `cpu-scheduling-qemu` 最後回頭看 execution trace 與 structured observability，理解會更完整

---

## 11. 結論

若用工程能力來總結這個 repository，它展示的不是單一 API 熟悉度，而是以下幾項更重要的能力：

1. 能從 user space 與 POSIX 基礎出發，掌握 process、FD、pipeline 與 command architecture。
2. 能進入 Linux kernel module，實作 `cdev`、`procfs`、`sysfs`、`ioctl` 與同步機制。
3. 能處理 embedded Linux bring-up 的 Device Tree、platform driver、initramfs 與 QEMU workflow。
4. 能設計共享記憶體、ring buffer、`mmap`、zero-copy 與 benchmark 對照路徑。
5. 能把 `Trace` 做成多層可觀測面，而不是只停留在零散 log。

若只挑一個最值得深入研究的主題，那就是：

> `Trace` 在這個 repository 中不是附屬品，而是設計核心。它貫穿了 driver bring-up、資料路徑驗證、排程模擬、IPC 比較與 benchmark 自動化。

這也是這份作品集最有技術辨識度的地方。
