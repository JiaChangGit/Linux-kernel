# Linux 字元裝置驅動技術報告

本報告說明 `chardev-driver` 的設計、執行流程、資料保護方式，以及開發時遇到的問題。內容以目前專案中的程式碼為準，重點是把 driver 的運作方式說清楚，讓流程能直接對回程式碼。

---

## 1. 專案目標與範圍

這個專案實作一個字元裝置驅動程式 (Character Device Driver)。模組載入後，系統會建立 `/dev/chardev0`，使用者空間程式可以用一般檔案操作與核心模組互動。

本專案涵蓋四個常見介面：

| 介面 | 英文 | 對外路徑或 API | 用途 |
|---|---|---|---|
| 字元裝置 | Character Device | `/dev/chardev0` | 讓使用者程式用 `read()` / `write()` 傳資料。 |
| 控制命令 | ioctl, Input/Output Control | `ioctl(fd, cmd, arg)` | 傳送控制命令，例如清空 buffer、取得長度、切換唯讀模式。 |
| 診斷檔案系統 | procfs | `/proc/chardev_info` | 查看 driver 目前狀態。 |
| 裝置屬性檔案系統 | sysfs | `/sys/class/chardev/chardev0/*` | 讀取或設定裝置屬性。 |

---

## 2. 整體架構

### 2.1 模組與檔案關係

```text
driver/chardev.h
  -> 定義 ioctl command
  -> 同時被 driver/chardev.c 與 userspace/test_app.c 使用

driver/chardev.c
  -> kernel module 主體
  -> 建立 /dev/chardev0
  -> 建立 /proc/chardev_info
  -> 建立 /sys/class/chardev/chardev0/*

userspace/test_app.c
  -> 使用 open/write/read/ioctl 測試 driver

scripts/load.sh
  -> 建置 chardev.ko
  -> 載入 kernel module
  -> 檢查 /dev/chardev0

scripts/unload.sh
  -> 卸載 kernel module
  -> 清理建置產物
```

### 2.2 核心資料結構

`driver/chardev.c` 使用一個全域 `drv` 結構保存 driver 狀態：

| 欄位 | 英文關鍵字 | 作用 |
|---|---|---|
| `buf` | Kernel Buffer | 存放使用者寫入的資料。 |
| `buf_len` | Buffer Length | 記錄目前有效資料長度。 |
| `read_only` | Read-only Flag | 控制是否允許寫入。 |
| `open_count` | Atomic Counter | 記錄 open 次數。 |
| `read_count` | Atomic Counter | 記錄 read 次數。 |
| `write_count` | Atomic Counter | 記錄 write 次數。 |
| `lock` | Mutex | 保護 buffer 讀寫與 reset。 |
| `devno` | Device Number | 保存 major/minor number。 |
| `cdev` | Character Device Object | 連接 VFS 與 driver callback。 |
| `cls` / `dev` | Class / Device | 建立 sysfs 與 `/dev` 節點。 |
| `proc_entry` | procfs Entry | 建立 `/proc/chardev_info`。 |

---

## 3. 初始化與清理流程

### 3.1 初始化流程 (Init Flow)

`module_init(chardev_init)` 指定 `chardev_init()` 為模組載入入口。執行 `insmod chardev.ko` 後，核心會呼叫這個函式。

流程如下：

```text
insmod chardev.ko
  -> module_init(chardev_init)
  -> kzalloc(BUF_SIZE, GFP_KERNEL)
  -> mutex_init(&drv.lock)
  -> atomic_set(...)
  -> alloc_chrdev_region(&drv.devno, 0, 1, DRIVER_NAME)
  -> cdev_init(&drv.cdev, &chardev_fops)
  -> cdev_add(&drv.cdev, drv.devno, 1)
  -> class_create(...)
  -> drv.cls->dev_groups = chardev_groups
  -> device_create(..., "chardev0")
  -> proc_create("chardev_info", 0444, NULL, &chardev_proc_ops)
  -> driver 可使用
```

幾個重要 API：

- `kzalloc()`：配置核心記憶體並清零。清零可避免讀到未初始化資料。
- `alloc_chrdev_region()`：動態取得 major/minor number，避免手動指定造成衝突。
- `cdev_init()` / `cdev_add()`：把 `struct file_operations` 註冊給 VFS。
- `class_create()` / `device_create()`：建立 sysfs device，並讓 udev 有機會建立 `/dev/chardev0`。
- `proc_create()`：建立 `/proc/chardev_info`。

### 3.2 錯誤回滾 (Error Unwind)

核心初始化常常是一連串資源申請。只要中間某一步失敗，就要把前面已經成功申請的資源釋放掉。

本專案使用 `goto` label 做反向清理：

```c
err_device:
  device_destroy(drv.cls, drv.devno);
err_class:
  class_destroy(drv.cls);
err_cdev:
  cdev_del(&drv.cdev);
err_region:
  unregister_chrdev_region(drv.devno, 1);
err_buf:
  kfree(drv.buf);
```

這種寫法在 kernel code 很常見，原因是：

- 每個錯誤點要清理的資源不同。
- 反向清理順序固定，較不容易漏掉。
- 避免多層 `if` 讓程式流程變得難讀。

### 3.3 卸載流程 (Exit Flow)

`module_exit(chardev_exit)` 指定卸載入口。執行 `rmmod chardev` 後，核心會呼叫 `chardev_exit()`。

```text
rmmod chardev
  -> module_exit(chardev_exit)
  -> proc_remove(drv.proc_entry)
  -> device_destroy(drv.cls, drv.devno)
  -> class_destroy(drv.cls)
  -> cdev_del(&drv.cdev)
  -> unregister_chrdev_region(drv.devno, 1)
  -> kfree(drv.buf)
```

清理順序的原則是：先移除外部可見介面，再釋放底層資源。這樣可以降低使用者空間還看得到節點、但 driver 資源已經被釋放的風險。

---

## 4. VFS 檔案操作

### 4.1 `struct file_operations`

`chardev_fops` 是 VFS 轉接表 (Dispatch Table)：

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

當使用者程式呼叫：

```c
read(fd, buf, count);
```

核心不是直接知道要執行 `chardev_read()`。實際流程是：

```text
read(fd, buf, count)
  -> VFS 找到 fd 對應的 struct file
  -> struct file 指向 chardev_fops
  -> 呼叫 chardev_fops.read
  -> 執行 chardev_read()
```

### 4.2 `write()` 資料流

```text
使用者字串
  -> write(fd, user_buf, count)
  -> chardev_write()
  -> 檢查 read_only
  -> 限制 count 不超過 BUF_SIZE
  -> mutex_lock()
  -> copy_from_user(drv.buf, user_buf, count)
  -> 更新 drv.buf_len
  -> mutex_unlock()
```

關鍵點：

- `copy_from_user()` 不能用 `memcpy()` 取代，因為來源指標在 user space。
- `count` 需要限制在 `BUF_SIZE` 內，避免寫超過 kernel buffer。
- `mutex` 保護 `drv.buf` 和 `drv.buf_len`，避免多個行程同時寫入造成資料交錯。
- 目前設計是覆蓋模式 (Overwrite Model)，不是 append。每次 write 都從 buffer 開頭重新寫入。

### 4.3 `read()` 資料流

```text
read(fd, user_buf, count)
  -> chardev_read()
  -> 檢查 *ppos 是否已到 EOF
  -> mutex_lock()
  -> 計算可讀長度
  -> copy_to_user(user_buf, drv.buf + *ppos, to_copy)
  -> 推進 *ppos
  -> mutex_unlock()
```

`*ppos` 是檔案位置 (File Position)。如果同一個 fd 已經讀到結尾，再讀一次會回傳 `0`，也就是 EOF。

這也是 `test_app.c` 在 `write()` 後要呼叫：

```c
lseek(fd, 0, SEEK_SET);
```

原因是 `write()` 完成後位置在資料尾端，若不把位置移回開頭，接著 `read()` 可能讀不到剛寫入的內容。

---

## 5. ioctl 控制介面

`ioctl()` 適合處理「不是資料流」的操作，例如設定模式、查詢狀態、清除資料。

本專案定義三個 command：

| Command | 巨集 | 方向 | 功能 |
|---|---|---|---|
| 清空 buffer | `_IO` | 無資料傳遞 | `IOCTL_RESET_BUF` |
| 取得長度 | `_IOR` | Kernel -> User | `IOCTL_GET_LEN` |
| 設定唯讀 | `_IOW` | User -> Kernel | `IOCTL_SET_RDONLY` |

`_IO`、`_IOR`、`_IOW` 的差異：

- `_IO`：只有命令，沒有額外資料。
- `_IOR`：driver 讀出資料給使用者，R 可理解成 Read from driver。
- `_IOW`：使用者寫資料給 driver，W 可理解成 Write to driver。
- `_IOWR`：雙向傳遞，本專案沒有使用。

`chardev_ioctl()` 先檢查：

```c
if (_IOC_TYPE(cmd) != CHARDEV_MAGIC) return -ENOTTY;
if (_IOC_NR(cmd) > CHARDEV_MAGIC_MAX) return -ENOTTY;
```

這樣可以避免不屬於本 driver 的 command 被誤處理。

---

## 6. procfs 與 sysfs

### 6.1 procfs：偏向診斷資訊

`/proc/chardev_info` 輸出多個欄位：

```text
buf_len
read_only
open_count
read_count
write_count
buf_content
```

本專案使用 `single_open()` 與 `seq_file`：

- `single_open()`：適合只輸出一次內容的 procfs 檔案。
- `seq_printf()`：透過 `seq_file` 安全輸出格式化文字。

procfs 適合放「人可以直接讀」的診斷資訊，不適合拿來當正式設定介面。

### 6.2 sysfs：偏向裝置屬性

`/sys/class/chardev/chardev0/` 下有三個屬性：

| 檔案 | 權限 | 對應 callback | 作用 |
|---|---|---|---|
| `buf_len` | Read-only | `buf_len_show()` | 顯示目前 buffer 長度。 |
| `read_only` | Read / Write | `read_only_show()` / `read_only_store()` | 讀取或設定唯讀模式。 |
| `stats` | Read-only | `stats_show()` | 顯示 open/read/write 次數。 |

sysfs 的習慣是「一個檔案代表一個屬性」。例如 `read_only` 只負責唯讀模式，不把所有狀態塞在同一個檔案中。

---

## 7. 同步設計

### 7.1 為什麼 buffer 要用 mutex

`drv.buf` 和 `drv.buf_len` 是所有行程共享的全域狀態。假設兩個行程同時寫入：

```text
Process A: write("AAAA")
Process B: write("BBBB")
```

如果沒有鎖，可能發生：

```text
drv.buf 前半段來自 A，後半段來自 B
drv.buf_len 又被另一個行程更新
```

這會造成資料內容與長度不一致。本專案用 `mutex_lock()` 包住主要 buffer 操作，讓同一時間只有一個執行路徑可以修改 buffer。

### 7.2 為什麼不用 spinlock

`copy_from_user()` 與 `copy_to_user()` 可能因 page fault 進入睡眠。睡眠的路徑不能放在 spinlock 內，否則可能造成核心鎖定問題。

比較：

| 同步工具 | 英文 | 適合情境 | 本專案是否使用 |
|---|---|---|---|
| Mutex | `struct mutex` | 可睡眠的臨界區，例如 user copy | 使用，用於 buffer。 |
| Spinlock | `spinlock_t` | 不能睡眠、很短的臨界區，例如中斷上下文 | 不使用。 |
| Atomic | `atomic_t` | 單一整數計數 | 使用，用於 open/read/write counters。 |

### 7.3 目前同步範圍的限制

目前 buffer copy/reset 主要路徑有 mutex，但以下讀取狀態的路徑沒有完全共用同一把鎖：

- `proc_show()` 讀取 `drv.buf_len`、`drv.buf`、`drv.read_only`。
- `buf_len_show()` 讀取 `drv.buf_len`。
- `IOCTL_GET_LEN` 讀取 `drv.buf_len`。
- `read_only_store()` 與 `IOCTL_SET_RDONLY` 都可更新 `drv.read_only`。

在這份簡化版 driver 中，這樣設計可讓程式保持簡潔；若要放進更嚴格的產品情境，應該把狀態讀寫也納入一致的同步策略。

---

## 8. 開發過程中的 BUG 與解法

### 8.1 BUG：`make -C driver` 時 `M=$(PWD)` 指到錯誤目錄

現象：

```text
make -C /lib/modules/.../build M=/home/user/Linux-kernel/chardev-driver modules
```

預期應該是：

```text
M=/home/user/Linux-kernel/chardev-driver/driver
```

原因：

- `driver/Makefile` 原本使用 `$(PWD)`。
- 當從專案根目錄執行 `make -C driver` 時，GNU Make 會切到 `driver` 目錄，但環境變數 `PWD` 可能仍保留原本目錄。
- kernel build system 依 `M=` 判斷外部模組原始碼位置，因此 `M` 指錯會造成建置路徑錯誤。

解法：

- 改用 GNU Make 的 `$(CURDIR)`。
- `CURDIR` 會反映 `make -C` 切換後的目前目錄。

修正後：

```make
$(MAKE) -C $(KDIR) M=$(CURDIR) modules
```

### 8.2 BUG：找不到 kernel build directory

現象：

```text
make[1]: *** /lib/modules/6.6.87.2-microsoft-standard-WSL2/build: No such file or directory. Stop.
```

原因：

- 編譯 kernel module 需要目前執行中 kernel 的 build directory。
- WSL 的 Microsoft kernel 常見沒有安裝對應的 build tree。

解法：

- 一般 Ubuntu kernel：安裝 `linux-headers-$(uname -r)`。
- WSL：使用有 headers 的環境，或準備相符的 WSL kernel source/build tree。

分析：

- 這不是 C 程式語法錯誤。
- 這是 kernel module build system 找不到外部模組所需的核心標頭與建置規則。

### 8.3 BUG：`class_create()` 在不同 kernel 版本參數不同

現象：

不同 kernel 可能出現：

```text
too many arguments to function 'class_create'
```

或：

```text
too few arguments to function 'class_create'
```

原因：

- Linux kernel API 會隨版本改動。
- 舊版常見 `class_create(THIS_MODULE, CLASS_NAME)`。
- 新版常見 `class_create(CLASS_NAME)`。

解法：

- 在 driver 中加入版本判斷：

```c
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
  drv.cls = class_create(CLASS_NAME);
#else
  drv.cls = class_create(THIS_MODULE, CLASS_NAME);
#endif
```

分析：

- driver 不是只要在一台機器能編譯就好。
- kernel API 版本差異是 driver 開發常見問題，文件中要清楚標示原因與處理方式。

### 8.4 BUG：讀取前忘記重設 file position

現象：

`write()` 後立刻 `read()`，結果讀不到資料或回傳 `0`。

原因：

- 同一個 fd 會保存 file position。
- `write()` 後 position 在資料尾端。
- `read()` 從目前 position 開始讀，因此可能直接遇到 EOF。

解法：

```c
lseek(fd, 0, SEEK_SET);
```

分析：

- 這是檔案位置語意造成的結果。
- `/dev/chardev0` 不是單純變數；VFS 仍會處理 fd 與 position。

### 8.5 BUG：Shell 顯示 `Permission denied` 容易誤判

現象：

```bash
echo "abc" > /dev/chardev0
bash: /dev/chardev0: Permission denied
```

原因可能有兩種：

1. `/dev/chardev0` 檔案權限不足。
2. driver 的 `read_only` 被設為 `1`，`chardev_write()` 回傳 `-EACCES`。

解法：

```bash
ls -l /dev/chardev0
cat /sys/class/chardev/chardev0/read_only
echo 0 | sudo tee /sys/class/chardev/chardev0/read_only
```

分析：

- Shell 只看到系統呼叫失敗，不會自動告訴你錯誤是檔案權限還是 driver 主動拒絕。
- 這也是加入 sysfs 狀態檔的價值：可以直接檢查 driver 內部狀態。

---

## 9. 實際操作範例

### 9.1 寫入、讀取、查看狀態

```bash
echo "kernel test" > /dev/chardev0
cat /dev/chardev0
cat /proc/chardev_info
cat /sys/class/chardev/chardev0/buf_len
```

資料流：

```text
echo
  -> write()
  -> chardev_write()
  -> copy_from_user()
  -> drv.buf

cat
  -> read()
  -> chardev_read()
  -> copy_to_user()
  -> terminal output
```

### 9.2 使用 sysfs 控制唯讀模式

```bash
echo 1 | sudo tee /sys/class/chardev/chardev0/read_only
echo "blocked" > /dev/chardev0
echo 0 | sudo tee /sys/class/chardev/chardev0/read_only
echo "allowed" > /dev/chardev0
```

### 9.3 使用 ioctl 控制 driver

`userspace/test_app.c` 會示範：

```c
ioctl(fd, IOCTL_GET_LEN, &len);
ioctl(fd, IOCTL_SET_RDONLY, &rdonly);
ioctl(fd, IOCTL_RESET_BUF);
```

適合用 ioctl 的原因：

- 這些操作是控制命令，不是普通資料流。
- command 有明確方向，例如 `_IOR` 回傳資料、`_IOW` 接收資料。
- C 程式可以用 header 中的 macro 與 driver 保持一致。

---

## 10. 總結

`chardev-driver` 是一個小型但完整的 Linux character device 範例。它示範了：

- 如何把 kernel module 註冊成字元裝置。
- 如何透過 VFS callback 處理 `open()`、`read()`、`write()`、`ioctl()`。
- 如何用 `copy_from_user()` / `copy_to_user()` 安全跨越 user space 與 kernel space。
- 如何用 procfs 和 sysfs 提供狀態觀察與屬性控制。
- 如何用 mutex 與 atomic counter 處理基本同步問題。
- 如何整理 driver 開發時常見的 build、API 版本、file position 與權限判讀問題。

後續若要把此專案擴充得更接近實務 driver，可以優先處理 blocking I/O、poll/epoll、多 minor device、per-open state，以及更完整的一致性鎖定策略。
