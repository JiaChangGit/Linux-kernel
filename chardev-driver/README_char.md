# Linux Character Device Driver：字元裝置驅動專案

![Kernel Version](https://img.shields.io/badge/Kernel-5.15%2B-blue.svg)
![License](https://img.shields.io/badge/License-GPL--2.0-green.svg)
![Platform](https://img.shields.io/badge/Platform-Ubuntu%2022.04-orange.svg)

本專案實作一個 Linux 字元裝置驅動程式 (Character Device Driver)。載入核心模組後，系統會建立 `/dev/chardev0`，使用者程式可以用一般檔案 I/O 呼叫 `open()`、`read()`、`write()` 和 `ioctl()` 與核心模組互動。

這份專案用一個小型驅動串起 Linux driver 常見的幾個介面：

- `/dev/chardev0`：字元裝置節點 (Device Node)，給使用者程式讀寫資料。
- `/proc/chardev_info`：procfs 診斷介面，查看目前 buffer、計數器和唯讀狀態。
- `/sys/class/chardev/chardev0/*`：sysfs 屬性介面，讀取狀態或切換 `read_only`。
- `ioctl()`：控制命令介面，提供清空 buffer、取得長度、切換唯讀模式。

---

## 專案可以學到什麼

| 關鍵字 | 英文 | 在本專案中的位置 | 說明 |
|---|---|---|---|
| 字元裝置 | Character Device | `driver/chardev.c` | 以位元組流方式讀寫的裝置，例如序列埠、終端機、虛擬 driver。 |
| 虛擬檔案系統 | VFS, Virtual File System | `struct file_operations` | Linux 把 `read()`、`write()` 先交給 VFS，再由 VFS 轉給 driver callback。 |
| 主設備號 / 次設備號 | Major / Minor Number | `alloc_chrdev_region()` | Major 決定由哪個 driver 處理，Minor 用來區分同一 driver 底下的不同裝置。 |
| 核心緩衝區 | Kernel Buffer | `drv.buf` | driver 保存在 kernel space 的資料區，不可直接讓使用者指標存取。 |
| 使用者空間 | User Space | `test_app.c` | 一般程式執行的位置，不能直接碰 kernel memory。 |
| 核心空間 | Kernel Space | `chardev.ko` | 核心模組執行的位置，錯誤存取可能造成系統不穩。 |
| 複製使用者資料 | `copy_from_user()` | `chardev_write()` | 從 user space 安全複製資料到 kernel space。 |
| 複製到使用者 | `copy_to_user()` | `chardev_read()` | 從 kernel space 安全複製資料回 user space。 |
| 互斥鎖 | Mutex | `drv.lock` | 保護共享 buffer，避免多個行程同時改資料造成內容交錯。 |
| 原子變數 | Atomic Variable | `atomic_t` | 用於簡單計數，例如 open/read/write 次數。 |
| 帶外控制 | ioctl, Input/Output Control | `chardev_ioctl()` | 適合傳送「控制命令」，不是一般資料流。 |
| procfs | Process Filesystem | `/proc/chardev_info` | 常用於輸出診斷資訊，方便 `cat` 查看。 |
| sysfs | System Filesystem | `/sys/class/chardev/...` | 常用於裝置屬性，一個檔案通常代表一個設定或狀態。 |

---

## 專案結構

```text
chardev-driver/
├── driver/
│   ├── chardev.c      # kernel module 主體
│   ├── chardev.h      # ioctl command 定義，driver 與 userspace 共用
│   └── Makefile       # out-of-tree kernel module 建置檔
├── userspace/
│   ├── test_app.c     # 使用者空間測試程式
│   └── Makefile       # 測試程式建置檔
├── scripts/
│   ├── load.sh        # 建置並載入 chardev.ko
│   └── unload.sh      # 卸載模組並清理建置產物
└── docs/
    └── *.png          # 操作截圖
```

---

## 整體運作圖

```text
使用者程式 / Shell
  |
  | open/read/write/ioctl
  v
/dev/chardev0
  |
  v
VFS
  |
  v
chardev_fops
  |
  +--> chardev_open()
  +--> chardev_read()
  +--> chardev_write()
  +--> chardev_ioctl()
  +--> chardev_release()

另外兩個觀察 / 控制入口：

cat /proc/chardev_info
  -> procfs -> proc_show()

cat/echo /sys/class/chardev/chardev0/read_only
  -> sysfs -> read_only_show() / read_only_store()
```

---

## 開發環境與前置需求

建議環境：

- Ubuntu 22.04 LTS 或相近版本。
- Linux kernel 5.15 以上。
- `gcc`、`make`。
- 與目前 kernel 版本相符的 kernel headers。

安裝常用工具：

```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r)
```

檢查 kernel header 是否存在：

```bash
ls /lib/modules/$(uname -r)/build
```

如果這個路徑不存在，kernel module 通常無法建置。WSL 的 Microsoft kernel 常見沒有對應 headers，這時可以改用一般 Ubuntu VM、安裝相符 headers，或自行準備 WSL kernel build tree。

---

## 建置與載入

### 1. 進入專案目錄

```bash
cd chardev-driver
```

### 2. 編譯 driver

```bash
make -C driver
```

成功後會產生 `driver/chardev.ko`。

也可以使用腳本一次完成建置與載入：

```bash
chmod +x scripts/*.sh
sudo ./scripts/load.sh
```

`load.sh` 會做幾件事：

1. 找出專案根目錄與 driver 目錄。
2. 透過 kernel build system 編譯 `chardev.ko`。
3. 用 `insmod` 載入模組。
4. 檢查 `/dev/chardev0` 是否建立。
5. 嘗試調整 `/dev/chardev0` 權限，方便測試。

---

## 驗證流程

建議開兩個終端機。Terminal 1 看 kernel log，Terminal 2 做操作。

### Terminal 1：觀察 driver log

```bash
sudo dmesg -w | grep chardev
```

### Terminal 2：寫入與讀取

```bash
echo "Hello Driver" > /dev/chardev0
cat /dev/chardev0
```

預期行為：

- `echo` 會進入 `chardev_write()`。
- `cat` 會進入 `chardev_read()`。
- Terminal 1 可看到類似 `write() 13 bytes`、`read() 13 bytes` 的 log。

### 查看 procfs 狀態

```bash
cat /proc/chardev_info
```

輸出會包含：

- `buf_len`：目前 buffer 有效資料長度。
- `read_only`：是否為唯讀模式，`0` 表示可寫，`1` 表示禁止寫入。
- `open_count`、`read_count`、`write_count`：操作次數。
- `buf_content`：目前 buffer 內容。

### 使用 sysfs 切換唯讀模式

```bash
cat /sys/class/chardev/chardev0/read_only
echo 1 | sudo tee /sys/class/chardev/chardev0/read_only
cat /sys/class/chardev/chardev0/read_only
```

切到唯讀後，再寫入會失敗：

```bash
echo "Try write" > /dev/chardev0
```

Shell 可能顯示：

```text
bash: /dev/chardev0: Permission denied
```

這通常是 driver 的 `chardev_write()` 回傳 `-EACCES`，不一定是檔案權限問題。先檢查：

```bash
cat /sys/class/chardev/chardev0/read_only
```

要恢復可寫：

```bash
echo 0 | sudo tee /sys/class/chardev/chardev0/read_only
```

### 執行 ioctl 測試程式

```bash
make -C userspace
sudo ./userspace/test_app
```

測試程式會依序執行：

1. `open("/dev/chardev0", O_RDWR)`
2. `write()`
3. `lseek(fd, 0, SEEK_SET)`
4. `read()`
5. `ioctl(IOCTL_GET_LEN)`
6. `ioctl(IOCTL_SET_RDONLY)`
7. 嘗試在唯讀模式寫入，確認被拒絕
8. `ioctl(IOCTL_RESET_BUF)`
9. `close()`

---

## 清理環境

```bash
sudo ./scripts/unload.sh
```

卸載後可確認節點已移除：

```bash
ls /dev/chardev0
ls /proc/chardev_info
```

正常情況下，這兩個路徑都應該不存在。

---

## 常見問題與除錯

### 1. `make` 找不到 `/lib/modules/.../build`

錯誤範例：

```text
make[1]: *** /lib/modules/$(uname -r)/build: No such file or directory. Stop.
```

原因：

- 沒有安裝目前 kernel 對應的 headers。
- WSL 使用 Microsoft kernel，系統裡不一定有對應 build tree。

解法：

- 在一般 Ubuntu 環境安裝 `linux-headers-$(uname -r)`。
- 若是 WSL，改用支援 kernel headers 的 VM 或自行準備 WSL kernel source/build tree。

### 2. `class_create()` 編譯錯誤

常見錯誤：

```text
too many arguments to function 'class_create'
```

或：

```text
too few arguments to function 'class_create'
```

原因：

- Linux kernel API 會隨版本調整。
- 舊版本常見寫法是 `class_create(THIS_MODULE, CLASS_NAME)`。
- 新版本常見寫法是 `class_create(CLASS_NAME)`。

解法：

- 在程式中用 `LINUX_VERSION_CODE` 判斷 kernel 版本。
- 讓 driver 依 kernel version 選擇正確寫法。

### 3. 寫入時顯示 `Permission denied`

可能原因有兩種：

- `/dev/chardev0` 的檔案權限不足。
- driver 已被切成唯讀模式 (`read_only = 1`)。

檢查方式：

```bash
ls -l /dev/chardev0
cat /sys/class/chardev/chardev0/read_only
```

如果 `read_only` 是 `1`：

```bash
echo 0 | sudo tee /sys/class/chardev/chardev0/read_only
```

### 4. 測試程式讀不到剛寫入的內容

原因：

- `write()` 後，file position 會在資料尾端。
- 同一個 file descriptor 若要重新讀取，需要先 `lseek(fd, 0, SEEK_SET)`。

本專案的 `test_app.c` 已示範這個步驟。

### 5. 模組已載入，重複 `insmod` 失敗

檢查模組：

```bash
lsmod | grep chardev
```

先卸載再載入：

```bash
sudo rmmod chardev
sudo ./scripts/load.sh
```

---

## 可延伸方向

1. **支援 poll/select/epoll**：加入等待佇列 (Wait Queue)，讓使用者程式可以等待資料可讀。
2. **支援多個 minor device**：例如 `/dev/chardev0`、`/dev/chardev1` 各自有獨立 buffer。
3. **加入 blocking read**：沒有資料時讓 `read()` 睡眠，而不是立刻回 EOF。
4. **加入 per-open state**：使用 `filp->private_data`，讓每個開啟的 fd 擁有自己的狀態。
5. **更完整的同步設計**：把 `read_only`、`buf_len` 的觀察路徑納入一致的鎖定策略。
