# Linux Kernel Portfolio Projects 技術報告

## 摘要

本專案是一組 Linux 系統軟體作品集，專案由三個子專案組成：

- `fwsh`：以 C 與 POSIX API 實作的迷你 Shell，展示命令解析、行程建立、Pipeline、I/O Redirection、Signal Handling 與韌體工程常用小工具。
- `chardev-driver`：Linux Character Device Driver，展示 `cdev`、`file_operations`、`copy_to_user()` / `copy_from_user()`、`ioctl`、`procfs`、`sysfs` 與 Kernel Module 生命週期。
- `qemu-platform-demo`：ARM64 QEMU 平台驅動示範，展示 Linux kernel cross-compilation、Device Tree overlay、Platform Driver、MMIO register model、initramfs 與 QEMU boot flow。

這三個專案合在一起，可以說明一個工程師如何從應用程式層理解 Process / File Descriptor，再往下延伸到 Linux Kernel Driver 的 VFS 介面，最後進入嵌入式 Linux 常見的 Device Tree 與 Platform Driver 工作流程。

## 專案技術地圖

| 子專案 | 技術領域 | 主要技術 | 重點能力 |
|---|---|---|---|
| `fwsh` | User-space system programming | C、POSIX、GNU Readline、`fork()`、`execvp()`、`pipe()`、`dup2()`、`waitpid()`、Signal | Shell REPL、命令解析、Pipeline 執行、背景行程、韌體工具 |
| `chardev-driver` | Linux kernel module / Character device | `alloc_chrdev_region()`、`cdev_add()`、`struct file_operations`、`ioctl`、`procfs`、`sysfs`、Mutex、Atomic | User-kernel interface、裝置檔 `/dev/chardev0`、狀態觀測與控制 |
| `qemu-platform-demo` | Embedded Linux / ARM64 / Platform driver | QEMU、ARM64 cross compile、Device Tree、DTB overlay、`platform_driver`、`of_match_table`、`devm_ioremap_resource()`、initramfs | 平台裝置描述、probe/remove 生命週期、sysfs 控制介面、虛擬硬體驗證 |

## 關鍵字教學與觀念整理

### User Space（使用者空間）與 Kernel Space（核心空間）

Linux 將程式執行環境分為 User Space 與 Kernel Space。User Space 是一般應用程式執行的位置，例如 `fwsh` 與 `test_app.c`；Kernel Space 則是 Linux 核心與驅動程式執行的位置，例如 `chardev.c` 與 `myled_ctrl.c`。

這個隔離非常重要。User Space 程式不能直接任意讀寫 Kernel memory，必須透過系統呼叫（System Call）或驅動介面溝通。例如 `chardev-driver` 中，使用者程式呼叫 `write(fd, buf, len)`，最後會進入驅動的 `.write = chardev_write`。驅動不能直接使用 user pointer，而是要透過 `copy_from_user()` 安全地把資料複製到 kernel buffer。

### VFS（Virtual File System，虛擬檔案系統）

Linux 把許多資源抽象成檔案，裝置也不例外。Character Device Driver 透過 VFS 將 `/dev/chardev0` 暴露給 User Space。使用者執行：

```bash
echo "Firmware Engineer" > /dev/chardev0
cat /dev/chardev0
```

看起來像在讀寫檔案，實際上 VFS 會把操作分派到驅動註冊的 `struct file_operations`：

```c
static const struct file_operations chardev_fops = {
    .owner = THIS_MODULE,
    .open = chardev_open,
    .release = chardev_release,
    .read = chardev_read,
    .write = chardev_write,
    .unlocked_ioctl = chardev_ioctl,
};
```

這是 Linux driver 的重要設計：User Space 使用一致的檔案 API，Kernel Driver 實作背後真正的行為。

### Device Tree（裝置樹）與 Platform Driver（平台驅動）

Device Tree 是一種描述硬體的資料結構，常見於 ARM / ARM64 embedded Linux。它回答一個問題：這台機器有哪些硬體？位址在哪裡？有哪些屬性？驅動程式不應把板子的硬體資訊寫死在 C code 裡，而應從 Device Tree 讀取。

在 `qemu-platform-demo/dts/myled-fragment.dts` 中，專案新增了一個虛擬 LED controller：

```dts
myled: myled-controller@10010000 {
    compatible        = "myvendor,myled-v1";
    reg               = <0x0 0x10010000 0x0 0x1000>;
    num-leds          = <4>;
    label             = "demo-rgb-led";
    default-brightness = <180>;
    status            = "okay";
};
```

其中 `compatible = "myvendor,myled-v1"` 會對應到 driver 中的 OF match table：

```c
static const struct of_device_id myled_of_match[] = {
    {.compatible = "myvendor,myled-v1"},
    {.compatible = "myvendor,myled"},
    {/* sentinel */}
};
```

當 Linux kernel 啟動並解析 DTB 後，若找到 compatible 相符的節點，就會建立 platform device，接著呼叫 platform driver 的 `probe()`。

### initramfs（Initial RAM Filesystem，初始記憶體檔案系統）

`initramfs` 是 kernel 啟動後最早掛載的根檔案系統之一。在本專案中，`qemu-platform-demo/scripts/04_build_rootfs.sh` 建立一個極小 root filesystem，把 BusyBox、`/init`、測試腳本與 `myled_ctrl.ko` 包進 `rootfs/initramfs.cpio.gz`。

QEMU 啟動時透過：

```bash
-initrd rootfs/initramfs.cpio.gz
-append "console=ttyAMA0 earlycon=pl011,0x9000000 rdinit=/init loglevel=7"
```

告訴 kernel：請載入這個 initramfs，並且執行 `/init` 作為第一個 user-space 程式。`/init` 會掛載 `/proc`、`/sys`、`/dev`，再 `insmod /myled_ctrl.ko` 載入 platform driver。

## 子專案一：fwsh Firmware Mini Shell

### 設計目標

`fwsh` 是一個以 firmware engineer 需求為中心的迷你 Shell。它不是只執行單一命令，而是實作 Shell 的核心能力：

- REPL（Read-Eval-Print Loop）
- GNU Readline command editing / history
- Pipeline：`cmd1 | cmd2 | cmd3`
- I/O Redirection：`<`、`>`、`>>`
- Background execution：`&`
- Signal handling：`SIGCHLD`、`SIGINT`、`SIGTSTP`
- Built-in commands：`cd`、`pwd`、`history`、`clear`、`help`、`exit`
- Firmware utilities：`hexdump`、`crc32`、`memmap`

### 核心資料結構

`fwsh/include/shell.h` 定義了兩個最重要的資料結構：`Cmd` 與 `Pipeline`。

`Cmd` 表示 pipeline 中的一段命令。例如：

```bash
cat firmware.bin | hexdump 0x100 > dump.txt
```

這裡有兩個 `Cmd`：第一個是 `cat firmware.bin`，第二個是 `hexdump 0x100 > dump.txt`。

`Cmd` 裡的欄位用途如下：

- `argv[MAX_ARGS]`：傳給 `execvp()` 的參數陣列，必須以 `NULL` 結尾。
- `argc`：參數數量。
- `in_file`：若命令有 `< file`，這裡保存輸入檔案名稱。
- `out_file`：若命令有 `> file` 或 `>> file`，這裡保存輸出檔案名稱。
- `out_append`：區分覆寫輸出 `>` 與附加輸出 `>>`。

`Pipeline` 則表示整行命令：

- `cmds[MAX_PIPES]`：最多 16 段 pipeline command。
- `ncmds`：實際命令數。
- `background`：是否使用 `&` 背景執行。

### Parser：如何把文字命令變成資料結構

`fwsh/src/parser.c` 使用一個簡單的 Lexer 掃描輸入字串。Lexer 保存：

- `input`：原始命令列字串。
- `pos`：目前掃描位置。

Parser 的核心流程是逐字掃描，遇到不同符號時執行不同語意：

- 遇到一般字串：放進目前 `Cmd.argv`。
- 遇到 `|`：切換到下一個 `Cmd`。
- 遇到 `<`：讀取後面的 filename，放入 `cur->in_file`。
- 遇到 `>`：讀取後面的 filename，放入 `cur->out_file`。
- 遇到 `>>`：同樣設定 `out_file`，但 `out_append = 1`。
- 遇到 `&`：設定 `pipeline->background = 1`。

例如輸入：

```bash
cat firmware.bin | hexdump 0x40 > dump.txt &
```

會被解析成：

```text
Pipeline
  ncmds = 2
  background = 1
  cmds[0]
    argv = ["cat", "firmware.bin", NULL]
  cmds[1]
    argv = ["hexdump", "0x40", NULL]
    out_file = "dump.txt"
    out_append = 0
```

Parser 也支援單引號與雙引號。單引號內的內容被視為 literal；雙引號內支援 `\"` 與 `\\` 這類基本 escape。這表示：

```bash
echo "hello world"
```

會把 `hello world` 當成同一個參數，而不是兩個參數。

### Executor：Pipeline 如何真正執行

`fwsh/src/executor.c` 是 Shell 最重要的部分，因為它把 `Pipeline` 轉換成 process 與 file descriptor 的連接圖。

以：

```bash
A | B | C
```

為例，需要兩組 pipe：

```text
A stdout -> pipe[0] write end
B stdin  <- pipe[0] read end
B stdout -> pipe[1] write end
C stdin  <- pipe[1] read end
```

Executor 的流程如下：

1. 計算 `npipes = ncmds - 1`。
2. 使用 `pipe(pipes[i])` 建立所有 pipe。
3. 對每個 command 呼叫 `fork()`。
4. 在 child process 中，根據 command 位置使用 `dup2()` 重新接線：
   - 若不是第一個 command：`dup2(pipes[i - 1][0], STDIN_FILENO)`。
   - 若不是最後一個 command：`dup2(pipes[i][1], STDOUT_FILENO)`。
5. Child 關閉所有不再需要的 pipe fd。
6. Child 執行 built-in 或 `execvp()`。
7. Parent 關閉所有 pipe fd。
8. 若不是背景執行，parent 用 `waitpid()` 等待 child 結束。

這裡最重要的是「關閉不需要的 file descriptor」。如果 parent 或某個 child 保留了 pipe 的 write end，讀端就可能永遠等不到 EOF，造成 pipeline 卡住。例如 `cat file | grep abc`，如果還有某個 process 沒關掉 write end，`grep` 可能以為後面還會有資料，因此不結束。

### I/O Redirection：`dup2()` 的角色

`setup_redirections()` 處理 `<`、`>`、`>>`：

- `< file`：`open(file, O_RDONLY)`，然後 `dup2(fd, STDIN_FILENO)`。
- `> file`：`open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644)`，然後 `dup2(fd, STDOUT_FILENO)`。
- `>> file`：`open(file, O_WRONLY | O_CREAT | O_APPEND, 0644)`，然後 `dup2(fd, STDOUT_FILENO)`。

`dup2(oldfd, newfd)` 的意思是：讓 `newfd` 指向 `oldfd` 所代表的 open file description。當 stdout 被換成檔案 fd 後，程式本身仍然只是呼叫 `printf()` 或寫 `STDOUT_FILENO`，但輸出會流到檔案。

### Built-in Dispatch Table

`fwsh/src/builtin.c` 使用 dispatch table 管理 built-in command：

```c
typedef struct {
  const char* name;
  int (*func)(Cmd*);
  const char* desc;
} BuiltinEntry;
```

這種設計的優點是擴充容易。要新增一個內建命令，不需要寫一大串 `if-else`，只要：

1. 實作 `static int builtin_xxx(Cmd* cmd)`。
2. 在 `builtins[]` 增加 `{ "xxx", builtin_xxx, "description" }`。

這是典型的 Function Pointer（函式指標）應用，也是一種簡單的 Command Dispatch Table（命令分派表）。

### Firmware Utility：hexdump

`hexdump` 用 16 bytes 為一列輸出：

- Offset：資料位移。
- Hex view：每個 byte 的 16 進位表示。
- ASCII view：可顯示字元直接顯示，不可顯示字元以 `.` 代替。

這對韌體開發很實用。例如檢查 firmware image 的 header、magic number、version field、checksum field。若某個 binary 開頭應該是 `0x7F 45 4C 46`，hexdump 可直接檢查它是否為 ELF。

### Firmware Utility：CRC-32

`crc32` 實作 IEEE 802.3 CRC-32，使用 table-driven 演算法。核心觀念是先建立 256 筆查表資料，之後每處理一個 byte 時用：

```text
crc = table[(crc XOR byte) & 0xFF] XOR (crc >> 8)
```

CRC-32 的常見用途：

- Bootloader 驗證 firmware image 是否損壞。
- OTA update 驗證下載內容是否完整。
- 通訊協定封包檢查，例如 UART / SPI frame。
- Flash dump 與 golden image 比對。

本專案使用 `0xEDB88320`，這是 IEEE CRC-32 常見的 reflected polynomial。初始值為 `0xFFFFFFFF`，最後再 XOR `0xFFFFFFFF`，這是標準 CRC-32 流程。

### Signal Handling

`fwsh/src/shell.c` 設定三種重要 signal：

- `SIGCHLD`：child process 結束時通知 parent。handler 使用 `waitpid(-1, NULL, WNOHANG)` 回收 zombie process。
- `SIGINT`：使用者按 `Ctrl+C`。Shell 本身不退出，而是清空目前輸入列並重新顯示 prompt。
- `SIGTSTP`：使用者按 `Ctrl+Z`。Shell 直接忽略，避免互動 Shell 自己被 suspend。

其中 `SIGCHLD` 對 background execution 很關鍵。如果背景 process 結束但 parent 沒有 wait，它會變成 zombie process。這裡透過 `WNOHANG` 非阻塞回收，避免 Shell 卡住。

## 子專案二：chardev-driver Character Device Driver

### 設計目標

`chardev-driver` 是一個自訂 Linux Character Device Driver，提供 `/dev/chardev0` 讓 User Space 讀寫。它不只實作 read/write，還提供三種常見 Linux driver interface：

- `/dev/chardev0`：主要資料通道，透過 VFS file operations。
- `/proc/chardev_info`：狀態觀測，適合輸出人類可讀的 driver 狀態。
- `/sys/class/chardev/chardev0/*`：屬性控制與查詢，適合單一設定值與統計值。
- `ioctl()`：命令式控制，例如 reset buffer、讀取長度、設定 read-only。

### Driver 狀態設計

`chardev.c` 使用一個 static 全域 `drv` 保存驅動狀態：

- `buf`：kernel buffer，大小 `BUF_SIZE = 4096`。
- `buf_len`：目前有效資料長度。
- `read_only`：是否拒絕寫入。
- `open_count`、`read_count`、`write_count`：使用 `atomic_t` 記錄操作次數。
- `lock`：`struct mutex`，保護 buffer 與長度。
- `devno`：major/minor device number。
- `cdev`：character device core structure。
- `cls`、`dev`：class 與 device，用於 sysfs 與 udev device node。
- `proc_entry`：`/proc/chardev_info` entry。

這個設計把「資料」、「同步」、「Linux 註冊資源」放在同一個 driver-private state 裡，方便 init 與 exit 統一管理。

### Module Init Trace

`chardev_init()` 的流程非常典型，值得逐步追：

1. `kzalloc(BUF_SIZE, GFP_KERNEL)` 配置 kernel buffer。
2. `mutex_init()` 初始化鎖。
3. `atomic_set()` 初始化統計計數器。
4. `alloc_chrdev_region(&drv.devno, 0, 1, DRIVER_NAME)` 動態取得 major/minor。
5. `cdev_init(&drv.cdev, &chardev_fops)` 綁定 file operations。
6. `cdev_add(&drv.cdev, drv.devno, 1)` 把 character device 加入 kernel。
7. `class_create(CLASS_NAME)` 建立 `/sys/class/chardev`。
8. 設定 `drv.cls->dev_groups = chardev_groups`，讓 device 建立時一起建立 sysfs attributes。
9. `device_create()` 建立 `/sys/class/chardev/chardev0`，也讓 udev 可建立 `/dev/chardev0`。
10. `proc_create("chardev_info", 0444, NULL, &chardev_proc_ops)` 建立 `/proc/chardev_info`。

若中途失敗，程式用 `goto err_xxx` 逆向釋放已取得的資源。這是 kernel code 常見寫法，因為 init 可能在任一階段失敗，必須保證不留下半初始化資源。

### Read Trace：`cat /dev/chardev0`

當使用者執行：

```bash
cat /dev/chardev0
```

大致流程是：

1. `cat` 呼叫 `open("/dev/chardev0", O_RDONLY)`。
2. VFS 呼叫 `chardev_open()`，`open_count` 增加。
3. `cat` 呼叫 `read(fd, ubuf, count)`。
4. VFS 呼叫 `chardev_read()`。
5. Driver 檢查 `*ppos >= drv.buf_len`，若已讀完則回傳 0，代表 EOF。
6. Driver 進入 `mutex_lock(&drv.lock)`。
7. 計算可複製長度：`min(drv.buf_len - *ppos, count)`。
8. 使用 `copy_to_user(ubuf, drv.buf + *ppos, to_copy)` 把 kernel buffer 複製到 user buffer。
9. 更新 `*ppos`，增加 `read_count`。
10. `mutex_unlock()`。
11. 回傳實際讀取 byte 數。

這裡的 `*ppos` 是 file position。若不更新它，`cat` 可能一直讀到同一段資料，無法得到 EOF。

### Write Trace：`echo "Firmware Engineer" > /dev/chardev0`

當使用者執行：

```bash
echo "Firmware Engineer" > /dev/chardev0
```

大致流程是：

1. Shell 開啟 `/dev/chardev0`。
2. `echo` 或 shell redirection 寫入資料。
3. VFS 呼叫 `chardev_write()`。
4. Driver 先檢查 `drv.read_only`。若為 1，回傳 `-EACCES`，User Space 會看到 permission 類錯誤。
5. 若 `count > BUF_SIZE`，限制最大寫入長度為 4096。
6. `mutex_lock()` 保護 buffer。
7. `copy_from_user(drv.buf, ubuf, count)` 把 user buffer 複製進 kernel buffer。
8. 更新 `drv.buf_len` 與 `*ppos`。
9. 增加 `write_count`。
10. `mutex_unlock()`。
11. 回傳實際寫入 byte 數。

`copy_from_user()` 可能沒有完整複製，所以程式用 `count - not_copied` 作為實際寫入長度。這是 kernel user access API 的正確思路：不能假設 user pointer 永遠有效。

### ioctl：命令式控制介面

`ioctl` 適合處理「不是單純 read/write」的控制命令。本專案在 `chardev.h` 定義：

```c
#define CHARDEV_MAGIC 'k'
#define IOCTL_RESET_BUF _IO(CHARDEV_MAGIC, 0)
#define IOCTL_GET_LEN _IOR(CHARDEV_MAGIC, 1, int)
#define IOCTL_SET_RDONLY _IOW(CHARDEV_MAGIC, 2, int)
```

三個 macro 的意思：

- `_IO`：不帶資料方向，適合 reset 這種命令。
- `_IOR`：Kernel to User，driver 回傳資料給 user。
- `_IOW`：User to Kernel，user 傳資料給 driver。

`chardev_ioctl()` 先檢查：

```c
if (_IOC_TYPE(cmd) != CHARDEV_MAGIC) return -ENOTTY;
if (_IOC_NR(cmd) > CHARDEV_MAGIC_MAX) return -ENOTTY;
```

這可以避免錯誤或不屬於本 driver 的 ioctl command 被誤處理。

三個 ioctl 的行為：

- `IOCTL_RESET_BUF`：清空 `drv.buf`，將 `buf_len` 設為 0。
- `IOCTL_GET_LEN`：把 `drv.buf_len` 透過 `copy_to_user()` 回傳。
- `IOCTL_SET_RDONLY`：透過 `copy_from_user()` 讀入 int，設定 `drv.read_only`。

### procfs：狀態快照

`/proc/chardev_info` 使用 `seq_file` 輸出 driver 狀態：

```text
=== chardev driver status ===
buf_len    : 18
read_only  : 0
open_count : 2
read_count : 1
write_count: 1
buf_content: Firmware Engineer
```

`seq_file` 是 kernel 推薦用於 procfs 輸出的機制之一。它比手動處理 offset 與 buffer 更安全，也更適合輸出多行狀態資訊。這裡搭配 `single_open()`，表示每次讀取只需要產生一份簡單內容。

### sysfs：屬性式控制

sysfs 的設計精神是「一個檔案對應一個屬性」。本 driver 提供：

- `buf_len`：read-only，顯示 buffer 長度。
- `read_only`：read-write，可讀取或設定 read-only mode。
- `stats`：read-only，顯示 open/read/write 次數。

例如：

```bash
cat /sys/class/chardev/chardev0/buf_len
echo 1 | sudo tee /sys/class/chardev/chardev0/read_only
cat /sys/class/chardev/chardev0/stats
```

`read_only_store()` 使用 `kstrtoint()` 將文字轉成整數，再用 `!!val` 轉成 0 或 1。這是 sysfs store callback 常見寫法，因為 sysfs 寫入本質上是文字。

## 子專案三：qemu-platform-demo ARM64 Platform Driver

### 設計目標

`qemu-platform-demo` 建立一個完整的 embedded Linux driver 驗證環境：

1. 下載並建置 ARM64 Linux kernel。
2. 從 QEMU virt machine dump 出 base DTB。
3. 將自訂 Device Tree overlay 合併到 base DTB。
4. 建置 out-of-tree platform driver `myled_ctrl.ko`。
5. 建立 BusyBox initramfs。
6. 用 QEMU 啟動 ARM64 Linux。
7. 在 initramfs 中載入 driver 並透過 sysfs 測試。

這非常接近真實 BSP（Board Support Package）或 driver bring-up 的流程，只是硬體由 QEMU 虛擬平台取代。

### Device Tree Overlay Trace

`qemu-platform-demo/dts/patch_dtb.sh` 的流程：

1. 使用 QEMU dump base DTB：

```bash
qemu-system-aarch64 \
    -machine virt,dumpdtb=qemu-virt-base.dtb \
    -cpu cortex-a57 \
    -kernel ${KERNEL} \
    -nographic
```

2. 使用 `dtc` 將 DTS overlay 編成 DTBO：

```bash
dtc -I dts -O dtb -@ -o myled-fragment.dtbo myled-fragment.dts
```

3. 使用 `fdtoverlay` 合併 base DTB 與 overlay：

```bash
fdtoverlay -i qemu-virt-base.dtb -o qemu-virt-myled.dtb myled-fragment.dtbo
```

4. 再用 `dtc -I dtb -O dts` 反編譯確認 `myled-controller` 節點存在。

這個流程的重點是：driver 不需要知道自己跑在 QEMU 或真板子上，它只依賴 Device Tree 提供的硬體描述。

### Platform Driver Probe Trace

`myled_ctrl.c` 的核心是：

```c
static struct platform_driver myled_driver = {
    .probe = myled_probe,
    .remove = myled_remove,
    .driver = {
        .name = "myled_ctrl",
        .of_match_table = myled_of_match,
        .pm = &myled_pm_ops,
    },
};

module_platform_driver(myled_driver);
```

`module_platform_driver()` 會產生 module init / exit glue code。當 module 載入後，kernel 會註冊這個 platform driver；若系統中已有 compatible 相符的 platform device，就會呼叫 `myled_probe()`。

`myled_probe()` 主要做以下事情：

1. `devm_kzalloc()` 配置 `struct myled_priv`。
2. 初始化 `spinlock_t lock`。
3. 使用 `of_property_read_u32()` 讀取 `num-leds`。
4. 使用 `of_property_read_string()` 讀取 `label`。
5. 使用 `platform_get_resource(pdev, IORESOURCE_MEM, 0)` 取得 `reg` 對應的 MMIO resource。
6. 使用 `devm_ioremap_resource()` 將 MMIO physical address 映射成 kernel virtual address。
7. 若沒有真實 MMIO 回應，切換為 simulated mode。
8. `platform_set_drvdata()` 與 `dev_set_drvdata()` 保存 private data。
9. 呼叫 `myled_hw_init()` 初始化控制器。
10. `sysfs_create_group()` 建立 `/sys/bus/platform/devices/.../myled/`。
11. `pm_runtime_enable()` 啟用 runtime PM 框架。

### Simulated MMIO：為什麼需要 shadow register

在真實硬體上，`reg = <0x0 0x10010000 0x0 0x1000>` 代表從 physical address `0x10010000` 開始的一段 MMIO register bank。Driver 可以透過 `readl()` / `writel()` 讀寫硬體暫存器。

但 QEMU virt machine 並沒有真的實作這個 LED controller。若 driver 直接讀不存在的硬體，可能得到無效值。因此 `myled_ctrl.c` 設計了 simulated mode：

- `priv->base`：真實 MMIO 模式使用。
- `priv->sim_regs[]`：模擬模式使用的 shadow register bank。
- `priv->simulated`：決定 read/write 走哪條路。

Register helper：

```c
static u32 myled_reg_read(struct myled_priv* priv, u32 off) {
  if (priv->simulated)
    val = priv->sim_regs[off / 4];
  else
    val = readl(priv->base + off);
}
```

這讓同一套上層 sysfs 與 driver logic 可以同時支援真實硬體與 QEMU demo。若未來有真正的 MMIO device，只要 Device Tree 的 `reg` 對應到有效硬體，driver 就可以改走 `readl()` / `writel()`。

### Register Model

`myled_ctrl.h` 定義了一組虛擬暫存器：

| Register | Offset | 用途 |
|---|---:|---|
| `MYLED_REG_CTRL` | `0x00` | 控制 enable、blink、PWM auto |
| `MYLED_REG_BRIGHTNESS` | `0x04` | 亮度，最大 255 |
| `MYLED_REG_COLOR` | `0x08` | RGB 顏色，格式為 `0xRRGGBB` |
| `MYLED_REG_STATUS` | `0x0C` | ready / fault 狀態 |
| `MYLED_REG_VERSION` | `0x10` | 硬體版本，預期 `0xAB01` |

Control register bit field：

- `MYLED_CTRL_ENABLE = BIT(0)`
- `MYLED_CTRL_BLINK = BIT(1)`
- `MYLED_CTRL_PWM_AUTO = BIT(2)`

Status register bit field：

- `MYLED_STATUS_READY = BIT(0)`
- `MYLED_STATUS_FAULT = BIT(1)`

這種 register offset + bit field 的寫法很接近真實 embedded driver。硬體規格書通常會列出 register map，driver 會用 macro 表示 offset 與 bit mask。

### sysfs Interface

Driver 建立一個 `myled` attribute group：

```text
/sys/bus/platform/devices/10010000.myled-controller/myled/
```

裡面包含：

- `enable`：讀寫 LED enable bit。
- `brightness`：讀寫亮度，範圍 0 到 255。
- `color`：讀寫 RGB 顏色，使用 16 進位文字，例如 `ff3300`。
- `blink`：讀寫 blink bit。
- `status`：讀取 ready / fault。
- `info`：輸出版本、LED 數量、simulated mode、control register、brightness、color。

例如：

```bash
echo 200 > /sys/bus/platform/devices/10010000.myled-controller/myled/brightness
cat /sys/bus/platform/devices/10010000.myled-controller/myled/brightness

echo ff3300 > /sys/bus/platform/devices/10010000.myled-controller/myled/color
cat /sys/bus/platform/devices/10010000.myled-controller/myled/color
```

`brightness_store()` 使用 `kstrtou32()` 將 sysfs 文字轉成 `u32`，並檢查是否大於 `MYLED_MAX_BRIGHTNESS`。若超過 255，回傳 `-EINVAL`。這是 driver interface 設計中很重要的一點：Kernel 端必須驗證 User Space 輸入，不可相信輸入永遠合法。

### QEMU Boot Trace

`qemu-platform-demo/scripts/05_run_qemu.sh` 啟動流程：

```bash
qemu-system-aarch64 \
    -machine virt \
    -cpu cortex-a57 \
    -m 512M \
    -nographic \
    -kernel linux-6.6.30/arch/arm64/boot/Image \
    -dtb dts/qemu-virt-myled.dtb \
    -initrd rootfs/initramfs.cpio.gz \
    -append "console=ttyAMA0 earlycon=pl011,0x9000000 rdinit=/init loglevel=7"
```

參數意義：

- `-machine virt`：使用 QEMU ARM virt machine。
- `-cpu cortex-a57`：模擬 ARM Cortex-A57 CPU。
- `-m 512M`：提供 512 MB RAM。
- `-nographic`：使用終端機 console，不開圖形視窗。
- `-kernel`：指定 ARM64 kernel Image。
- `-dtb`：指定已合併 myled overlay 的 DTB。
- `-initrd`：指定 initramfs。
- `-append`：傳給 kernel 的 bootargs。

其中 `rdinit=/init` 讓 kernel 在 initramfs 中執行 `/init`。`/init` 掛載 procfs、sysfs、devtmpfs，載入 `myled_ctrl.ko`，然後執行 `/test_myled.sh`。

### 測試腳本 Trace

`rootfs/overlay/test_myled.sh` 會：

1. 在 `/sys/bus/platform/devices` 找名稱包含 `10010000` 的 device。
2. 設定 `MYLED="${SYSFS_BASE}/${DEV}/myled"`。
3. 檢查 `info`、`enable`、`brightness`、`color`、`blink`、`status` 是否存在。
4. 寫入 `brightness = 200` 並讀回確認。
5. 寫入 `color = ff3300` 並讀回確認。
6. 寫入 `blink = 1` 並讀回確認。
7. 寫入 `enable = 0` 並讀回確認。
8. 輸出 `info` 與相關 `dmesg`。

這是一個很好理解的 driver validation pattern：每個 sysfs attribute 都用 write-then-read-back 驗證，並用 dmesg 補充 driver 內部 trace。

## 三個專案的技術連結

這三個子專案不是彼此獨立的玩具，而是同一條 Linux 系統軟體路徑上的不同層次。

第一層，`fwsh` 建立 User Space 基礎。它處理 process、file descriptor、pipe、signal。這些是理解 Linux 的必要能力，因為 Linux 幾乎所有抽象都會回到 process 與 file。

第二層，`chardev-driver` 進入 Kernel Space。它展示當 User Space 呼叫 `open()`、`read()`、`write()`、`ioctl()` 時，Kernel Driver 如何接住這些操作。這補上了「系統呼叫背後發生什麼」的理解。

第三層，`qemu-platform-demo` 把 driver 放進一個 embedded platform。它展示 Linux kernel 如何透過 Device Tree 找到硬體，如何呼叫 platform driver 的 `probe()`，以及如何在 initramfs 中完成最小系統啟動與驗證。

可以用以下 trace 串起來：

```text
User command
  -> fwsh parser
  -> fwsh executor
  -> Linux syscall
  -> VFS
  -> chardev file_operations
  -> kernel buffer / procfs / sysfs / ioctl

QEMU boot
  -> ARM64 kernel Image
  -> DTB with myled node
  -> platform device creation
  -> myled platform_driver probe
  -> sysfs attributes
  -> initramfs test script
```

## 實作細節

### 錯誤處理與資源回收

Kernel module 的 init path 特別需要嚴格的 error unwinding。`chardev_init()` 使用 `goto err_*` 逐層釋放資源，這是 Linux kernel 常見風格。原因是 kernel 沒有 User Space 那種自動 process teardown 保護；若 module init 失敗但沒有釋放 major number、cdev、class 或 buffer，會污染 kernel 狀態。

`myled_ctrl.c` 使用 `devm_kzalloc()` 與 `devm_ioremap_resource()`，這是 Device Managed Resource（裝置生命週期管理資源）。當 device remove 時，devm 資源會自動釋放，可降低 remove path 漏釋放的風險。

### 同步機制：Mutex、Spinlock、Atomic

`chardev-driver` 使用：

- `struct mutex lock`：保護可睡眠上下文中的 buffer 存取。
- `atomic_t`：統計 open/read/write 次數。

`qemu-platform-demo` 使用：

- `spinlock_t lock`：保護 register read/write。Register access 通常希望短時間完成，不應睡眠，因此用 spinlock 更符合低階 driver 習慣。

Mutex（互斥鎖）可以睡眠，適合一般 process context。Spinlock（自旋鎖）不可睡眠，適合短 critical section。Atomic（原子操作）適合簡單計數，避免為了加一就上鎖。

### User-kernel 資料交換

`chardev-driver` 中有兩種典型資料交換：

- `copy_from_user()`：User Space 到 Kernel Space。
- `copy_to_user()`：Kernel Space 到 User Space。

這兩者都是 Linux driver 的基本功。不能直接 dereference user pointer，因為 user pointer 可能無效、可能跨頁、可能造成 page fault，也可能是惡意傳入的地址。

### sysfs 與 procfs 的分工

`procfs` 適合輸出一段狀態報告，例如 `/proc/chardev_info`。它可以一次顯示 buffer 長度、read-only 狀態、計數器與內容。

`sysfs` 適合穩定、單一、可腳本化的屬性。例如：

```bash
echo 1 > read_only
cat buf_len
cat stats
```

在 `myled_ctrl` 中，`brightness`、`color`、`blink`、`enable` 都很適合 sysfs，因為每個屬性都有明確的單一語意。

## 限制與可改進方向

### fwsh

目前 parser 是自行實作的簡化 Shell grammar，適合展示核心概念，但不是完整 POSIX shell。可改進方向：

- 支援 environment variable expansion，例如 `$HOME`。
- 支援 command substitution，例如 `$(cmd)`。
- 支援更完整 quote / escape 規則。
- 加入 job control，例如 `fg`、`bg`、process group、terminal control。
- 對 parser 加入單元測試，測試 pipeline、quote、redirection、background 組合。

### chardev-driver

目前 driver 使用單一全域 buffer，適合 demo，但若要更接近 production driver，可改進：

- 支援多個 minor device，每個 device 有獨立 private data。
- 在 `open()` 中設定 `filp->private_data`，避免所有 file operation 都依賴全域 `drv`。
- 對 `read_only` 存取也加上同步保護，避免競態。
- 對 read/write partial copy 行為做更完整錯誤處理。
- 增加 poll/select 或 blocking read，使它更像真實資料流裝置。

### qemu-platform-demo

目前 `myled_ctrl` 的 simulated mode 很適合 QEMU demo，但它不是完整硬體模型。可改進方向：

- 在 QEMU 中實作一個真正 MMIO device model，讓 `readl()` / `writel()` 對應到 QEMU 裝置。
- 使用 Device Tree binding 文件描述 `myvendor,myled-v1` 的屬性格式。
- 增加 KUnit 或 shell-based regression test。
- 將 `brightness`、`color` 等介面改接 Linux LED subsystem，讓它符合 kernel 既有框架。
- 補上 runtime PM 的實際 usage count 與 autosuspend 流程。

## 結論

本專案涵蓋 Linux 系統軟體從 User Space 到 Kernel Space，再到 ARM64 embedded platform bring-up 的完整學習線。`fwsh` 展示 process、file descriptor、pipeline 與 signal 的基礎；`chardev-driver` 展示 VFS 如何把 `/dev` 操作接到 kernel driver；`qemu-platform-demo` 展示 Device Tree 如何描述硬體，以及 platform driver 如何在 QEMU ARM64 環境中被載入、probe、建立 sysfs 介面並被測試。
