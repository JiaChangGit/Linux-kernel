# Character Device Driver API 技術分析報告

本報告只根據 `/chardev-driver` 目前實際存在的內容進行分析。分析優先順序為 source code、header file、build system、script、README/comment、實際呼叫流程。本次更新沿用原 report 的兩階段結構：先做 Codebase Trace，再整理 Architecture / API Technical Report；重點補上可驗證的 execution flow、callback chain、ownership / lifecycle、error path 與風險分析。

以下內容分成：

- `# Direct Observation`：可直接從目前程式碼、Makefile 或 scripts 驗證。
- `# Conservative Inference`：基於呼叫關係或 Linux driver model 的保守推論，會明確標示。
- 若無法從現有內容確認，會直接寫「目前程式碼中未觀察到」或「無法從現有內容確認」。

---

## 第一階段：Codebase Trace

### 1. Project Structure

#### # Direct Observation

| 類別 | 檔案 | 角色 |
|---|---|---|
| source file | `driver/chardev.c` | kernel module 主體，實作 char device、VFS callbacks、ioctl、procfs、sysfs、module init/exit 與 cleanup unwind。 |
| header file | `driver/chardev.h` | 定義 ioctl magic number、`IOCTL_RESET_BUF`、`IOCTL_GET_LEN`、`IOCTL_SET_RDONLY`、`CHARDEV_MAGIC_MAX`。kernel driver 與 userspace test 共用此 header。 |
| build system | `driver/Makefile` | out-of-tree kernel module build，`obj-m += chardev.o`，target `install` 直接 `insmod chardev.ko`，`uninstall` 直接 `rmmod chardev`。 |
| userspace source | `userspace/test_app.c` | 使用 `/dev/chardev0` 測試 `open`、`write`、`lseek`、`read`、`ioctl`、`close`。 |
| userspace build | `userspace/Makefile` | 使用 `gcc -Wall -Wextra -I../driver` 編譯 `test_app.c`。 |
| scripts | `scripts/load.sh` | 根據目前內容：計算 project root、driver dir、kernel build dir，執行 `make -C "$KDIR" M="$DRIVER_DIR" modules`，`sudo insmod "$DRIVER_DIR/chardev.ko" || true`，檢查 `/dev/chardev0`，嘗試 chmod，顯示 modinfo 與 dmesg。 |
| scripts | `scripts/unload.sh` | 根據目前內容：`sudo rmmod chardev`，檢查 `/dev/chardev0` 與 `/proc/chardev_info` 是否移除，印 dmesg tail，進入 `driver` 執行 `make clean`。 |
| docs | `docs/*.png` | demo/build 截圖；本報告未用圖片內容推導 driver 行為。 |
| README/report | `README_char.md`、`report_char.md` | 說明性文件，僅作低優先級佐證。 |

#### Module / Component Relationship

```text
driver/chardev.h
  -> ioctl macro shared by driver/chardev.c and userspace/test_app.c

driver/chardev.c
  -> builds chardev.ko via driver/Makefile or scripts/load.sh
  -> module_init(chardev_init)
  -> creates:
       /dev/chardev0               via cdev + class/device_create
       /sys/class/chardev/chardev0 via class/device model + dev_groups
       /proc/chardev_info          via proc_create

userspace/test_app.c
  -> open("/dev/chardev0", O_RDWR)
  -> write/read/ioctl/close
  -> shares ioctl command definitions from ../driver/chardev.h

scripts/load.sh
  -> make kernel module
  -> insmod chardev.ko
  -> verifies /dev/chardev0

scripts/unload.sh
  -> rmmod chardev
  -> verifies /dev/chardev0 and /proc/chardev_info cleanup
  -> make clean
```

---

### 2. Semantic Element Extraction

#### # Direct Observation

以下只列目前實際存在的元素。

| 類型 | 名稱 | 定義位置 | 說明 |
|---|---|---|---|
| registration macro | `module_init(chardev_init)` | `driver/chardev.c:326` | module load entry point。 |
| registration macro | `module_exit(chardev_exit)` | `driver/chardev.c:327` | module unload entry point。 |
| callback | `chardev_open` | `driver/chardev.c:62` | VFS `.open` callback，增加 `open_count`。 |
| callback | `chardev_release` | `driver/chardev.c:69` | VFS `.release` callback，只印 log。 |
| callback | `chardev_read` | `driver/chardev.c:74` | VFS `.read` callback，從 `drv.buf` 複製到 userspace。 |
| callback | `chardev_write` | `driver/chardev.c:94` | VFS `.write` callback，從 userspace 複製到 `drv.buf`。 |
| callback | `chardev_ioctl` | `driver/chardev.c:119` | VFS `.unlocked_ioctl` callback，處理 reset/get length/set read-only。 |
| operation table / dispatch table | `chardev_fops` | `driver/chardev.c:158` | `struct file_operations`，把 VFS operations 綁到上述 callbacks。 |
| callback | `proc_show` | `driver/chardev.c:171` | seq_file show callback，輸出 buffer/state/counters。 |
| callback | `proc_open` | `driver/chardev.c:183` | procfs `.proc_open` callback，呼叫 `single_open(file, proc_show, NULL)`。 |
| operation table / dispatch table | `chardev_proc_ops` | `driver/chardev.c:187` | `struct proc_ops`，綁定 procfs open/read/lseek/release。 |
| callback | `buf_len_show` | `driver/chardev.c:200` | sysfs read-only `buf_len` show callback。 |
| callback | `read_only_show` | `driver/chardev.c:207` | sysfs `read_only` show callback。 |
| callback | `read_only_store` | `driver/chardev.c:212` | sysfs `read_only` store callback，透過 `kstrtoint` 更新 `drv.read_only`。 |
| callback | `stats_show` | `driver/chardev.c:223` | sysfs read-only `stats` show callback。 |
| sysfs attribute macro | `DEVICE_ATTR_RO(buf_len)` | `driver/chardev.c:204` | 建立 `buf_len` read-only attribute。 |
| sysfs attribute macro | `DEVICE_ATTR_RW(read_only)` | `driver/chardev.c:220` | 建立 `read_only` read/write attribute。 |
| sysfs attribute macro | `DEVICE_ATTR_RO(stats)` | `driver/chardev.c:229` | 建立 `stats` read-only attribute。 |
| sysfs attribute group macro | `ATTRIBUTE_GROUPS(chardev)` | `driver/chardev.c:238` | 產生 `chardev_groups`，由 `drv.cls->dev_groups` 使用。 |
| global state | anonymous `static struct drv` | `driver/chardev.c:34` | 儲存 buffer、state、counters、lock、device handles、proc entry。 |
| synchronization primitive | `struct mutex lock` | `driver/chardev.c:44` | 保護 buffer copy/reset path。 |
| synchronization primitive | `atomic_t open_count/read_count/write_count` | `driver/chardev.c:39-41` | 計數 open/read/write 次數。 |
| memory management | `kzalloc(BUF_SIZE, GFP_KERNEL)` | `driver/chardev.c:248` | 初始化時配置 4096 bytes kernel buffer。 |
| memory management | `kfree(drv.buf)` | `driver/chardev.c:310`、`322` | init error unwind 與 module exit 釋放 buffer。 |
| registration mechanism | `alloc_chrdev_region` | `driver/chardev.c:258` | 動態取得 dev_t major/minor。 |
| registration mechanism | `cdev_init` / `cdev_add` | `driver/chardev.c:266-268` | 將 `chardev_fops` 註冊到 char device。 |
| registration mechanism | `class_create` | `driver/chardev.c:277` | 建立 `/sys/class/chardev` class。 |
| registration mechanism | `device_create` | `driver/chardev.c:286` | 建立 `chardev0` device；udev 可能建立 `/dev/chardev0`。 |
| registration mechanism | `proc_create` | `driver/chardev.c:293` | 建立 `/proc/chardev_info`。 |
| communication mechanism | `copy_to_user` | `driver/chardev.c:82`、`137` | read 與 IOCTL_GET_LEN 從 kernel 複製資料到 userspace。 |
| communication mechanism | `copy_from_user` | `driver/chardev.c:107`、`142` | write 與 IOCTL_SET_RDONLY 從 userspace 複製資料到 kernel。 |
| external interface | `/dev/chardev0` | created via `device_create` | VFS char device interface。 |
| external interface | `/proc/chardev_info` | created via `proc_create` | procfs status interface。 |
| external interface | `/sys/class/chardev/chardev0/{buf_len,read_only,stats}` | class `dev_groups` + `device_create` | sysfs control/status interface。 |
| ioctl macro | `IOCTL_RESET_BUF` | `driver/chardev.h:10` | `_IO(CHARDEV_MAGIC, 0)`，清空 buffer。 |
| ioctl macro | `IOCTL_GET_LEN` | `driver/chardev.h:11` | `_IOR(CHARDEV_MAGIC, 1, int)`，取得資料長度。 |
| ioctl macro | `IOCTL_SET_RDONLY` | `driver/chardev.h:12` | `_IOW(CHARDEV_MAGIC, 2, int)`，設定 read-only mode。 |
| compiler / section annotation | `__init` | `driver/chardev.c:244` | 標示 init function。 |
| compiler / section annotation | `__exit` | `driver/chardev.c:316` | 標示 exit function。 |
| user pointer annotation | `char __user *`、`int __user *` | `driver/chardev.c:74`、`94`、`137`、`142` | 標示 userspace pointer。 |

#### 目前程式碼中未觀察到

- `.llseek` file operation；但 userspace test 使用 `lseek(fd, 0, SEEK_SET)`。
- `.poll` / `.read_iter` / `.write_iter`。
- wait queue、completion、spinlock、rwlock、RCU。
- IRQ、tasklet、workqueue、timer。
- mmap、DMA、async notification、fasync。
- per-open private state，例如 `filp->private_data`。
- 多 minor device；`alloc_chrdev_region(..., 1, ...)` 只註冊一個 device number。

---

### 3. API / Macro Inventory

#### Initialization

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct/data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `module_init(chardev_init)` | macro | `driver/chardev.c:326` | module load | `insmod chardev.ko` 或 Makefile `install` | 設定 module 初始化入口 | `chardev_init` | module 載入時開始配置 buffer、註冊 char device/proc/sysfs。 |
| `chardev_init` | init callback | `driver/chardev.c:244` | module load | `module_init` | 建立 driver 所需所有 resource | global `drv` | 成功後 driver 進入 operational state；失敗走 goto unwind。 |
| `kzalloc` | memory API | call at `driver/chardev.c:248` | `chardev_init` | module init | 配置 4096 bytes buffer 並清零 | `drv.buf`、`BUF_SIZE` | 失敗直接回傳 `-ENOMEM`，其他 resource 不會建立。 |
| `mutex_init` | sync init API | call at `driver/chardev.c:252` | `chardev_init` | module init | 初始化 `drv.lock` | `drv.lock` | read/write/reset path 可進入 mutex critical section。 |
| `atomic_set` | atomic init API | call at `driver/chardev.c:253-255` | `chardev_init` | module init | 初始化 open/read/write counters | `drv.open_count`、`drv.read_count`、`drv.write_count` | proc/sysfs stats 有初始值。 |

#### Registration

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct/data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `alloc_chrdev_region` | registration API | call at `driver/chardev.c:258` | `chardev_init` | module init | 動態配置 major/minor | `drv.devno` | 成功後可註冊 `cdev`；失敗釋放 buffer。 |
| `cdev_init` | registration API | call at `driver/chardev.c:266` | `chardev_init` | module init | 初始化 `struct cdev` 並綁定 file operations | `drv.cdev`、`chardev_fops` | 決定 VFS indirect dispatch target。 |
| `cdev_add` | registration API | call at `driver/chardev.c:268` | `chardev_init` | module init | 將 `cdev` 加入 kernel char device registry | `drv.devno` | 成功後 VFS 可依 dev_t 找到 fops。 |
| `class_create` | registration API | call at `driver/chardev.c:277` | `chardev_init` | module init | 建立 device class | `drv.cls` | 後續 device_create 與 sysfs class/device path 依賴它。 |
| `drv.cls->dev_groups = chardev_groups` | registration binding | `driver/chardev.c:283` | `chardev_init` | module init | 把 sysfs attribute groups 綁到 class device creation | `chardev_groups` | `device_create` 建立 device 時帶出 sysfs attributes。 |
| `device_create` | registration API | call at `driver/chardev.c:286` | `chardev_init` | module init | 建立 `chardev0` device | `drv.dev`、`drv.devno` | 建立 `/sys/class/chardev/chardev0`；udev 可建立 `/dev/chardev0`。 |
| `proc_create` | registration API | call at `driver/chardev.c:293` | `chardev_init` | module init | 建立 procfs entry | `drv.proc_entry`、`chardev_proc_ops` | 讓 `/proc/chardev_info` 可讀。 |

#### Execution Path

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct/data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `chardev_fops` | operation table | `driver/chardev.c:158` | `cdev_init` | VFS indirect dispatch | 綁定 `.open/.release/.read/.write/.unlocked_ioctl` | `chardev_open/read/write/ioctl/release` | 所有 `/dev/chardev0` runtime I/O 都由此 table dispatch。 |
| `chardev_open` | callback | `driver/chardev.c:62` | VFS `.open` | userspace `open("/dev/chardev0")` | 增加 `open_count` 並 log | `drv.open_count` | 不建立 per-open state；只改 global counter。 |
| `chardev_read` | callback | `driver/chardev.c:74` | VFS `.read` | userspace `read()` / shell `cat` | 依 `*ppos` 從 `drv.buf` 複製資料到 userspace | `drv.buf`、`drv.buf_len`、`drv.lock`、`drv.read_count` | `*ppos >= drv.buf_len` 回 EOF；成功 read 會增加 offset 與 counter。 |
| `chardev_write` | callback | `driver/chardev.c:94` | VFS `.write` | userspace `write()` / shell redirection | 檢查 read-only，限制 count，複製資料到 `drv.buf` | `drv.read_only`、`drv.buf`、`drv.buf_len`、`drv.lock`、`drv.write_count` | write 覆蓋 buffer，設定 `*ppos = drv.buf_len`。 |
| `chardev_ioctl` | callback | `driver/chardev.c:119` | VFS `.unlocked_ioctl` | userspace `ioctl()` | 驗證 magic / nr 並 dispatch 三個 command | `drv.buf`、`drv.buf_len`、`drv.read_only` | reset/get length/set read-only 的控制入口。 |

#### Lifecycle / Cleanup

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct/data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `module_exit(chardev_exit)` | macro | `driver/chardev.c:327` | module unload | `rmmod chardev` | 設定 module cleanup 入口 | `chardev_exit` | 卸載時依序釋放 proc/device/class/cdev/devno/buffer。 |
| `chardev_exit` | exit callback | `driver/chardev.c:316` | module unload | `module_exit` | 正常 cleanup path | global `drv` | 釋放所有 init 成功後存在的 resource。 |
| `proc_remove` | cleanup API | call at `driver/chardev.c:317` | `chardev_exit` | module unload | 移除 `/proc/chardev_info` | `drv.proc_entry` | procfs path 消失。 |
| `device_destroy` | cleanup API | call at `driver/chardev.c:318`、`304` | exit / init unwind | module unload / init failure | 移除 device | `drv.cls`、`drv.devno` | `/sys/class/chardev/chardev0` 與 `/dev/chardev0` 對應 device 被移除。 |
| `class_destroy` | cleanup API | call at `driver/chardev.c:319`、`306` | exit / init unwind | module unload / init failure | 移除 class | `drv.cls` | `/sys/class/chardev` class cleanup。 |
| `cdev_del` | cleanup API | call at `driver/chardev.c:320`、`308` | exit / init unwind | module unload / init failure | 移除 cdev registration | `drv.cdev` | VFS 不再 dispatch 到本 driver fops。 |
| `unregister_chrdev_region` | cleanup API | call at `driver/chardev.c:321`、`310` | exit / init unwind | module unload / init failure | 釋放 dev_t region | `drv.devno` | major/minor number 歸還 kernel。 |
| `kfree` | cleanup API | call at `driver/chardev.c:322`、`312` | exit / init unwind | module unload / init failure | 釋放 kernel buffer | `drv.buf` | 避免 buffer leak。 |

#### Memory Handling

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct/data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `BUF_SIZE` | macro | `driver/chardev.c:30` | `kzalloc`、write clamp、reset | init/write/ioctl | 固定 buffer 上限 4096 bytes | `drv.buf` | write count 大於 BUF_SIZE 時被截斷。 |
| `copy_from_user` | userspace copy API | call at `driver/chardev.c:107`、`142` | write / ioctl set rdonly | VFS runtime | 從 userspace 複製資料到 kernel | `drv.buf`、`val` | 回傳未複製 bytes 或失敗；write 仍以部分成功更新 len。 |
| `copy_to_user` | userspace copy API | call at `driver/chardev.c:82`、`137` | read / ioctl get len | VFS runtime | 從 kernel 複製資料到 userspace | `drv.buf`、`val` | read 依未複製 bytes 回傳實際 copied bytes；ioctl 失敗回 `-EFAULT`。 |
| `memset` | memory API | call at `driver/chardev.c:129` | IOCTL_RESET_BUF | ioctl runtime | 清空 buffer | `drv.buf`、`BUF_SIZE` | reset 後 `drv.buf_len = 0`。 |

#### Synchronization

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct/data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `struct mutex lock` | mutex | `driver/chardev.c:44` | read/write/reset | VFS runtime | 保護 buffer copy/reset 與 `buf_len` 更新 | `drv.buf`、`drv.buf_len` | read/write/reset 不會同時改動 buffer。 |
| `atomic_t open_count` | atomic counter | `driver/chardev.c:39` | open/proc/sysfs stats | VFS/proc/sysfs | 記錄 open 次數 | `drv.open_count` | 無需 mutex 即可讀寫 counter。 |
| `atomic_t read_count` | atomic counter | `driver/chardev.c:40` | read/proc/sysfs stats | VFS/proc/sysfs | 記錄 read 次數 | `drv.read_count` | read 成功或部分成功後增加。 |
| `atomic_t write_count` | atomic counter | `driver/chardev.c:41` | write/proc/sysfs stats | VFS/proc/sysfs | 記錄 write 次數 | `drv.write_count` | write 成功或部分成功後增加。 |

#### Event Dispatch

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 關聯 struct/data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|---|
| `chardev_proc_ops` | operation table | `driver/chardev.c:187` | `proc_create` | procfs runtime | 綁定 procfs open/read/lseek/release | `proc_open`、`seq_read`、`seq_lseek`、`single_release` | `cat /proc/chardev_info` 經 procfs dispatch 到 `proc_show`。 |
| `proc_open` | callback | `driver/chardev.c:183` | procfs `.proc_open` | `open("/proc/chardev_info")` | 呼叫 `single_open` 綁定 `proc_show` | `proc_show` | seq_file machinery 後續呼叫 `proc_show`。 |
| `proc_show` | callback | `driver/chardev.c:171` | `single_open` | procfs read | 輸出 driver 狀態 | `drv.buf_len`、`drv.read_only`、atomic counters、`drv.buf` | procfs status snapshot。 |
| `chardev_attrs[]` | sysfs attribute table | `driver/chardev.c:232` | `ATTRIBUTE_GROUPS` | sysfs runtime | 聚合 buf_len/read_only/stats | `dev_attr_*` | 決定 sysfs 暴露的 attribute。 |
| `ATTRIBUTE_GROUPS(chardev)` | sysfs group macro | `driver/chardev.c:238` | `drv.cls->dev_groups` | device creation | 產生 class default device attribute groups | `chardev_attrs` | `device_create` 後 attributes 出現在 device sysfs path。 |

#### Logging / Debug

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|
| `pr_info` | logging API | 多處 | open/release/read/write/ioctl/init/exit | runtime/init/exit | 印 driver 狀態與 I/O 行為 | 不改變 control flow。 |
| `pr_warn` | logging API | `driver/chardev.c:98` | `chardev_write` | write when read-only | 記錄 write 被 read-only mode 擋下 | 伴隨回傳 `-EACCES`。 |
| `pr_err` | logging API | `driver/chardev.c:260`、`270` | init failure | init | 記錄 alloc/cdev_add failure | 伴隨 goto cleanup。 |
| `seq_printf` | procfs output API | `driver/chardev.c:172-177` | `proc_show` | procfs read | 格式化輸出 procfs status | 不改變 state。 |
| `sysfs_emit` | sysfs output API | `driver/chardev.c:202`、`210`、`225` | sysfs show callbacks | sysfs read | 格式化輸出 sysfs attribute | 不改變 state。 |

#### Error Handling

| 名稱 | 類型 | 定義位置 | 呼叫位置 | 呼叫來源 | 用途 | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|
| `-ENOMEM` | error code | `driver/chardev.c:248`、`294` | `chardev_init` | init | buffer 或 proc entry 建立失敗 | module load 失敗，依階段 cleanup。 |
| `-ENOTTY` | error code | `driver/chardev.c:123-124`、`151` | `chardev_ioctl` | invalid ioctl | 拒絕 magic/nr/default command | ioctl call fail，不改 state。 |
| `-EACCES` | error code | `driver/chardev.c:100` | `chardev_write` | read-only mode | 拒絕 write | 不進入 copy_from_user，不增加 write_count。 |
| `-EFAULT` | error code | `driver/chardev.c:137`、`143` | `chardev_ioctl` | copy_to/from_user failure | userspace pointer copy 失敗 | GET_LEN / SET_RDONLY 不完整時回錯誤。 |
| `-EINVAL` | error code | `driver/chardev.c:216` | `read_only_store` | sysfs parse failure | sysfs input 不是 int | 不更新 read_only。 |
| goto labels | unwind path | `driver/chardev.c:301-312` | `chardev_init` | init error | 依已建立 resource 反向釋放 | 避免 init 中途失敗造成 leak。 |

---

### 4. Call Graph

#### Initialization Chain

```text
scripts/load.sh
  -> make -C "$KDIR" M="$DRIVER_DIR" modules
  -> sudo insmod "$DRIVER_DIR/chardev.ko" || true
  -> kernel module loader
  -> module_init(chardev_init)
  -> chardev_init
       -> drv.buf = kzalloc(BUF_SIZE, GFP_KERNEL)
       -> mutex_init(&drv.lock)
       -> atomic_set(open/read/write counters, 0)
       -> alloc_chrdev_region(&drv.devno, 0, 1, DRIVER_NAME)
       -> cdev_init(&drv.cdev, &chardev_fops)
       -> drv.cdev.owner = THIS_MODULE
       -> cdev_add(&drv.cdev, drv.devno, 1)
       -> class_create(CLASS_NAME)
       -> drv.cls->dev_groups = chardev_groups
       -> device_create(drv.cls, NULL, drv.devno, NULL, "chardev0")
       -> proc_create("chardev_info", 0444, NULL, &chardev_proc_ops)
       -> operational
```

#### Runtime Chain: `/dev/chardev0` read/write/ioctl

```text
userspace open("/dev/chardev0", O_RDWR)
  -> VFS
  -> cdev registered at drv.devno
  -> chardev_fops.open
  -> chardev_open
       -> atomic_inc(&drv.open_count)

userspace write(fd, user_buf, count)
  -> VFS
  -> chardev_fops.write
  -> chardev_write
       -> if drv.read_only: return -EACCES
       -> if count > BUF_SIZE: count = BUF_SIZE
       -> mutex_lock(&drv.lock)
       -> copy_from_user(drv.buf, ubuf, count)
       -> drv.buf_len = count - not_copied
       -> *ppos = drv.buf_len
       -> atomic_inc(&drv.write_count)
       -> mutex_unlock(&drv.lock)
       -> return count - not_copied

userspace read(fd, user_buf, count)
  -> VFS
  -> chardev_fops.read
  -> chardev_read
       -> if *ppos >= drv.buf_len: return 0
       -> mutex_lock(&drv.lock)
       -> to_copy = min(drv.buf_len - *ppos, count)
       -> copy_to_user(ubuf, drv.buf + *ppos, to_copy)
       -> *ppos += copied
       -> atomic_inc(&drv.read_count)
       -> mutex_unlock(&drv.lock)
       -> return copied

userspace ioctl(fd, cmd, arg)
  -> VFS
  -> chardev_fops.unlocked_ioctl
  -> chardev_ioctl
       -> validate _IOC_TYPE(cmd) == CHARDEV_MAGIC
       -> validate _IOC_NR(cmd) <= CHARDEV_MAGIC_MAX
       -> switch(cmd)
```

#### Runtime Chain: ioctl Commands

```text
IOCTL_RESET_BUF
  -> mutex_lock
  -> memset(drv.buf, 0, BUF_SIZE)
  -> drv.buf_len = 0
  -> mutex_unlock

IOCTL_GET_LEN
  -> val = drv.buf_len
  -> copy_to_user((int __user *)arg, &val, sizeof(val))

IOCTL_SET_RDONLY
  -> copy_from_user(&val, (int __user *)arg, sizeof(val))
  -> drv.read_only = !!val
```

#### Runtime Chain: procfs

```text
cat /proc/chardev_info
  -> procfs open
  -> chardev_proc_ops.proc_open
  -> proc_open
  -> single_open(file, proc_show, NULL)
  -> seq_read
  -> proc_show
       -> seq_printf buf_len/read_only/counters/buf_content
  -> single_release
```

#### Runtime Chain: sysfs

```text
cat /sys/class/chardev/chardev0/buf_len
  -> sysfs dispatch
  -> buf_len_show
  -> sysfs_emit("%d\n", drv.buf_len)

cat /sys/class/chardev/chardev0/read_only
  -> read_only_show
  -> sysfs_emit("%d\n", drv.read_only)

echo 1 > /sys/class/chardev/chardev0/read_only
  -> read_only_store
  -> kstrtoint(buf, 10, &val)
  -> drv.read_only = !!val

cat /sys/class/chardev/chardev0/stats
  -> stats_show
  -> atomic_read(open/read/write counters)
```

#### Cleanup Chain

```text
scripts/unload.sh
  -> sudo rmmod chardev
  -> module_exit(chardev_exit)
  -> chardev_exit
       -> proc_remove(drv.proc_entry)
       -> device_destroy(drv.cls, drv.devno)
       -> class_destroy(drv.cls)
       -> cdev_del(&drv.cdev)
       -> unregister_chrdev_region(drv.devno, 1)
       -> kfree(drv.buf)
  -> scripts/unload.sh checks /dev/chardev0 and /proc/chardev_info
  -> make clean
```

#### Callback Chain

```text
VFS callbacks:
  chardev_fops.open           -> chardev_open
  chardev_fops.release        -> chardev_release
  chardev_fops.read           -> chardev_read
  chardev_fops.write          -> chardev_write
  chardev_fops.unlocked_ioctl -> chardev_ioctl

procfs callbacks:
  chardev_proc_ops.proc_open    -> proc_open -> single_open(... proc_show ...)
  chardev_proc_ops.proc_read    -> seq_read
  chardev_proc_ops.proc_lseek   -> seq_lseek
  chardev_proc_ops.proc_release -> single_release

sysfs callbacks:
  dev_attr_buf_len.show       -> buf_len_show
  dev_attr_read_only.show     -> read_only_show
  dev_attr_read_only.store    -> read_only_store
  dev_attr_stats.show         -> stats_show
```

#### Indirect Call Chain / Dispatch Table

| Dispatch point | Table / function pointer | Target | Evidence |
|---|---|---|---|
| char device VFS dispatch | `struct file_operations chardev_fops` | `chardev_open/read/write/ioctl/release` | `driver/chardev.c:158-165` |
| cdev binding | `cdev_init(&drv.cdev, &chardev_fops)` | `chardev_fops` | `driver/chardev.c:266` |
| procfs dispatch | `struct proc_ops chardev_proc_ops` | `proc_open`、`seq_read`、`seq_lseek`、`single_release` | `driver/chardev.c:187-192` |
| proc show binding | `single_open(file, proc_show, NULL)` | `proc_show` | `driver/chardev.c:184` |
| sysfs dispatch | `DEVICE_ATTR_*` generated attributes | `buf_len_show`、`read_only_show/store`、`stats_show` | `driver/chardev.c:204`、`220`、`229` |
| sysfs group binding | `drv.cls->dev_groups = chardev_groups` | `chardev_attrs[]` via `ATTRIBUTE_GROUPS` | `driver/chardev.c:232-238`、`283` |

---

### 5. Struct / Resource Tracing

#### `static struct { ... } drv`

##### # Direct Observation

`drv` 是 `driver/chardev.c:34-54` 的匿名 static global struct。欄位如下：

| 欄位 | 初始化 / allocation | 使用位置 | ownership / lifetime |
|---|---|---|---|
| `char *buf` | `kzalloc(BUF_SIZE, GFP_KERNEL)` at `driver/chardev.c:248` | read/write/ioctl reset/proc_show | driver 擁有，module exit 或 init unwind 用 `kfree` 釋放。 |
| `int buf_len` | static zero-init；write 更新；reset 設 0 | read、write、GET_LEN、buf_len_show、proc_show | global state，生命週期跟 module 一致。 |
| `int read_only` | static zero-init；ioctl/sysfs store 更新 | write gate、proc/sysfs show | global mode flag，生命週期跟 module 一致。 |
| `atomic_t open_count` | `atomic_set(..., 0)` | open、proc_show、stats_show | global counter，module lifetime。 |
| `atomic_t read_count` | `atomic_set(..., 0)` | read、proc_show、stats_show | global counter，module lifetime。 |
| `atomic_t write_count` | `atomic_set(..., 0)` | write、proc_show、stats_show | global counter，module lifetime。 |
| `struct mutex lock` | `mutex_init` | read/write/RESET_BUF | global lock，module lifetime。 |
| `dev_t devno` | `alloc_chrdev_region` | cdev_add/device_create/destroy/unregister | major/minor ownership 由 driver 註冊並在 exit/unwind 歸還。 |
| `struct cdev cdev` | `cdev_init` / `cdev_add` | VFS dispatch | `cdev_del` 後解除 VFS binding。 |
| `struct class *cls` | `class_create` | device_create/destroy/class_destroy | driver 擁有 class pointer，exit/unwind destroy。 |
| `struct device *dev` | `device_create` | 目前只保存，未在 runtime callbacks 直接使用 | device object 由 `device_destroy` 移除。 |
| `struct proc_dir_entry *proc_entry` | `proc_create` | `proc_remove` | proc entry 由 driver create/remove。 |

#### Allocation / Init

```text
module load
  -> static drv zero-initialized by kernel
  -> drv.buf allocated by kzalloc
  -> drv.lock initialized
  -> atomic counters set 0
  -> drv.devno allocated
  -> drv.cdev initialized and added
  -> drv.cls created
  -> drv.cls->dev_groups bound
  -> drv.dev created
  -> drv.proc_entry created
```

#### Ownership

- `drv.buf`：driver 手動擁有；`kzalloc` 後必須 `kfree`。
- `drv.devno`：driver 透過 `alloc_chrdev_region` 取得；必須 `unregister_chrdev_region`。
- `drv.cdev`：struct 內嵌在 `drv`，但 registry binding 需 `cdev_del` 解除。
- `drv.cls`：`class_create` 回傳 pointer；必須 `class_destroy`。
- `drv.dev`：`device_create` 建立 device；必須 `device_destroy`。
- `drv.proc_entry`：`proc_create` 建立；必須 `proc_remove`。

#### Lifetime / Release Timing

正常 lifetime：

```text
chardev_init success
  -> resource active until module unload
  -> chardev_exit release in reverse-ish order:
       proc_remove
       device_destroy
       class_destroy
       cdev_del
       unregister_chrdev_region
       kfree
```

init failure unwind：

```text
proc_create failure
  -> err_device: device_destroy
  -> err_class: class_destroy
  -> err_cdev: cdev_del
  -> err_region: unregister_chrdev_region
  -> err_buf: kfree

device_create failure
  -> err_class -> err_cdev -> err_region -> err_buf

class_create failure
  -> err_cdev -> err_region -> err_buf

cdev_add failure
  -> err_region -> err_buf

alloc_chrdev_region failure
  -> err_buf
```

#### State Transition

```text
Zero-initialized
  -> Buffer allocated, buf_len=0, read_only=0, counters=0
  -> Registered but not fully visible while init still running
  -> Operational after proc_create success
  -> Runtime write:
       read_only=0 -> buf overwritten, buf_len set, write_count++
       read_only=1 -> return -EACCES, no buffer update
  -> Runtime read:
       ppos < buf_len -> copy out, ppos advances, read_count++
       ppos >= buf_len -> EOF
  -> IOCTL_RESET_BUF:
       buffer zeroed, buf_len=0
  -> IOCTL_SET_RDONLY or sysfs read_only_store:
       read_only toggled
  -> Exit:
       external entries removed, memory released
```

#### Data Passing Path

```text
userspace write buffer
  -> write(fd, ubuf, count)
  -> chardev_write
  -> copy_from_user(drv.buf, ubuf, count)
  -> drv.buf_len = count - not_copied
  -> visible via:
       read(fd, ...)
       ioctl(IOCTL_GET_LEN)
       cat /proc/chardev_info
       cat /sys/class/chardev/chardev0/buf_len

kernel buffer
  -> chardev_read
  -> copy_to_user(ubuf, drv.buf + *ppos, to_copy)
  -> userspace rbuf

read_only mode
  -> ioctl(IOCTL_SET_RDONLY, &val) or sysfs read_only_store
  -> drv.read_only = !!val
  -> chardev_write gate checks drv.read_only
```

#### Callback Binding

- `cdev_init(&drv.cdev, &chardev_fops)` binds VFS operation table before `cdev_add` exposes it.
- `proc_create(PROC_ENTRY_NAME, 0444, NULL, &chardev_proc_ops)` binds procfs operation table.
- `DEVICE_ATTR_*` macros create attributes; `chardev_attrs[]` collects them; `ATTRIBUTE_GROUPS(chardev)` generates `chardev_groups`; `drv.cls->dev_groups = chardev_groups` binds sysfs groups to devices created under the class.

---

### 6. Execution Trace

#### Initialization Flow

```text
[Build/Load]
scripts/load.sh
  -> derive ROOT_DIR, DRIVER_DIR, KDIR
  -> verify driver dir and Makefile
  -> make -C KDIR M=DRIVER_DIR modules
  -> sudo insmod DRIVER_DIR/chardev.ko || true
  -> ls -la /dev/chardev0
  -> chmod 666 /dev/chardev0

[Kernel init]
module_init(chardev_init)
  -> allocate kernel buffer
  -> initialize mutex and atomic counters
  -> allocate devno
  -> bind file_operations through cdev
  -> create class and attach dev_groups
  -> create chardev0 device
  -> create /proc/chardev_info
```

#### Runtime Flow

```text
[userspace/test_app.c]
open /dev/chardev0
  -> chardev_open
write "Hello from userspace!"
  -> chardev_write
lseek fd to 0
read
  -> chardev_read
ioctl GET_LEN
  -> chardev_ioctl -> IOCTL_GET_LEN
ioctl SET_RDONLY
  -> chardev_ioctl -> IOCTL_SET_RDONLY
write "blocked write"
  -> chardev_write -> -EACCES if read_only was set
ioctl SET_RDONLY off
ioctl RESET_BUF
  -> chardev_ioctl -> IOCTL_RESET_BUF
lseek + read after reset
close
  -> chardev_release
```

#### Cleanup Flow

```text
scripts/unload.sh
  -> sudo rmmod chardev
  -> module_exit(chardev_exit)
  -> remove proc entry
  -> destroy device and class
  -> delete cdev
  -> unregister devno
  -> free buffer
  -> verify /dev/chardev0 and /proc/chardev_info
  -> make clean
```

#### Event Flow

```text
VFS event:
  open/read/write/ioctl/close on /dev/chardev0
  -> file_operations dispatch
  -> global drv state mutation or observation

procfs event:
  read /proc/chardev_info
  -> proc_ops dispatch
  -> single_open/seq_read
  -> proc_show snapshot

sysfs event:
  read buf_len/read_only/stats
  -> sysfs show callback
  write read_only
  -> sysfs store callback
  -> update drv.read_only
```

#### Ownership Transfer

目前程式碼中沒有複雜 buffer ownership transfer。`copy_from_user` / `copy_to_user` 是資料複製，不是 ownership 轉移。可驗證 ownership 是：

- driver 擁有 `drv.buf`，userspace 永遠只提供來源/目的 user pointer。
- kernel registry 擁有已註冊的 cdev/class/device/proc entry，但 driver 保存 handle 並負責 unregister/destroy/remove。
- `test_app.c` 擁有 userspace fd；kernel driver 不保存 fd 或 per-open private data。

---

## 第二階段：Architecture / API Technical Report

### 1. Entry Point 行為

#### # Direct Observation

此 module 的 entry point 是 `module_init(chardev_init)`。目前 scripts 的載入路徑是 `scripts/load.sh:26` 先建置 module，再於 `scripts/load.sh:29` 使用 `sudo insmod "$DRIVER_DIR/chardev.ko" || true` 載入。`|| true` 代表 script 不會因 `insmod` 失敗而停止；這是 script 行為，不是 kernel module 行為。

`chardev_init` 不是只註冊一個 `/dev` 介面，而是同時建立三個 external interfaces：

1. char device：`cdev_init` / `cdev_add` + `device_create`，對外是 `/dev/chardev0`。
2. procfs：`proc_create("chardev_info", 0444, NULL, &chardev_proc_ops)`，對外是 `/proc/chardev_info`。
3. sysfs：`drv.cls->dev_groups = chardev_groups` 後 `device_create`，對外是 `/sys/class/chardev/chardev0/{buf_len,read_only,stats}`。

初始化任何一段失敗時會走 goto label unwind，並依照已建立 resource 的階段做 cleanup。

---

### 2. Callback Registration Chain

#### VFS / Character Device

```text
chardev_init
  -> cdev_init(&drv.cdev, &chardev_fops)
  -> cdev_add(&drv.cdev, drv.devno, 1)
  -> VFS operations on dev_t dispatch through chardev_fops
```

`chardev_fops` 是 char device 的主要 operation table：

- `.open = chardev_open`
- `.release = chardev_release`
- `.read = chardev_read`
- `.write = chardev_write`
- `.unlocked_ioctl = chardev_ioctl`

目前程式碼中未觀察到 `.llseek`；userspace test 使用 `lseek(fd, 0, SEEK_SET)`，因此 offset 行為依 VFS 預設處理。無法從 driver 內部確認 `.llseek` 是否符合所有預期，因為 driver 沒有自訂它。

#### procfs

```text
chardev_init
  -> proc_create(PROC_ENTRY_NAME, 0444, NULL, &chardev_proc_ops)
  -> proc_open
  -> single_open(file, proc_show, NULL)
  -> seq_read
  -> proc_show
```

procfs path 是 read-only mode (`0444`)。`proc_show` 會讀取 `drv.buf_len`、`drv.read_only`、atomic counters 與 `drv.buf` 內容。

#### sysfs

```text
DEVICE_ATTR_RO(buf_len)
DEVICE_ATTR_RW(read_only)
DEVICE_ATTR_RO(stats)
  -> chardev_attrs[]
  -> ATTRIBUTE_GROUPS(chardev)
  -> chardev_groups
  -> drv.cls->dev_groups = chardev_groups
  -> device_create(...)
```

sysfs 的 `read_only` 與 ioctl 的 `IOCTL_SET_RDONLY` 都會改同一個 `drv.read_only`。這是可驗證的 shared state path。

---

### 3. Runtime Dispatch Flow

#### `/dev/chardev0` write

`chardev_write` 先檢查 `drv.read_only`。若為 true，直接回傳 `-EACCES`，不進入 mutex、不 copy、不增加 `write_count`。若可寫，`count` 超過 `BUF_SIZE` 時會被裁到 4096，接著在 mutex 內執行 `copy_from_user`，用新的內容覆蓋 `drv.buf`，並將 `drv.buf_len` 設為成功複製 bytes。

這裡不是 append model；每次 write 都從 `drv.buf` 開頭覆蓋，並把 `*ppos` 設到新的 `drv.buf_len`。

#### `/dev/chardev0` read

`chardev_read` 先在 mutex 外檢查 `*ppos >= drv.buf_len`，符合時回傳 0 表示 EOF。否則進入 mutex，計算 `to_copy = min(drv.buf_len - *ppos, count)`，再 `copy_to_user`。完成後以成功複製 bytes 推進 `*ppos`，並增加 `read_count`。

#### ioctl

`chardev_ioctl` 先檢查 `_IOC_TYPE(cmd) == CHARDEV_MAGIC`，再檢查 `_IOC_NR(cmd) <= CHARDEV_MAGIC_MAX`。符合後用 switch 分派：

- `IOCTL_RESET_BUF`：在 mutex 內 `memset` buffer 並將 `buf_len` 設為 0。
- `IOCTL_GET_LEN`：把 `drv.buf_len` 複製到 userspace int pointer。
- `IOCTL_SET_RDONLY`：從 userspace int pointer 複製值，設定 `drv.read_only = !!val`。

#### procfs / sysfs

procfs 與 sysfs 都是觀察 global `drv` state；sysfs 的 `read_only_store` 另有 control role。procfs 使用 `seq_printf` 輸出多欄位；sysfs 使用 `sysfs_emit` 分別輸出單一 attribute 或 counters。

---

### 4. Indirect Call Path

#### # Direct Observation

本專案的 indirect dispatch 主要有三層：

1. VFS 透過 `struct file_operations chardev_fops` dispatch 到 char device callbacks。
2. procfs 透過 `struct proc_ops chardev_proc_ops` dispatch 到 `proc_open`，再經 `single_open` 綁定 `proc_show`。
3. sysfs 透過 `DEVICE_ATTR_*` 產生的 device attributes dispatch 到 show/store callbacks。

目前程式碼中未觀察到手寫 event loop、IRQ dispatch、workqueue callback 或 timer callback。

---

### 5. Resource Lifecycle

#### Manual Resource Model

此 driver 沒有使用 `devm_*`；所有核心 resource 都由 module init/exit 手動管理。這也是 init failure path 需要 goto unwind 的原因。

```text
kzalloc
  -> kfree
alloc_chrdev_region
  -> unregister_chrdev_region
cdev_add
  -> cdev_del
class_create
  -> class_destroy
device_create
  -> device_destroy
proc_create
  -> proc_remove
```

#### External Interface Lifecycle

| Interface | 建立位置 | 移除位置 | 備註 |
|---|---|---|---|
| `/dev/chardev0` | `device_create` + userspace udev behavior | `device_destroy` | script 以 `ls /dev/chardev0` 驗證。 |
| `/sys/class/chardev/chardev0/*` | `drv.cls->dev_groups` + `device_create` | `device_destroy` / `class_destroy` | attribute group 由 class dev_groups 帶出。 |
| `/proc/chardev_info` | `proc_create` | `proc_remove` | proc entry pointer 存於 `drv.proc_entry`。 |

#### State Lifecycle

`drv.buf_len`、`drv.read_only` 與 counters 都是 module-global state。`open()` 不建立 instance state，`release()` 也不清理 per-open state。因此多個 file descriptor 共享同一份 buffer、read_only flag 與 counters。

---

### 6. Error Propagation Path

#### Initialization Error Path

| 失敗點 | 回傳值 | cleanup |
|---|---|---|
| `kzalloc` 失敗 | `-ENOMEM` | 直接 return，尚無 resource。 |
| `alloc_chrdev_region` 失敗 | ret | `kfree(drv.buf)`。 |
| `cdev_add` 失敗 | ret | `unregister_chrdev_region` + `kfree`。 |
| `class_create` 失敗 | `PTR_ERR(drv.cls)` | `cdev_del` + `unregister_chrdev_region` + `kfree`。 |
| `device_create` 失敗 | `PTR_ERR(drv.dev)` | `class_destroy` + `cdev_del` + `unregister_chrdev_region` + `kfree`。 |
| `proc_create` 失敗 | `-ENOMEM` | `device_destroy` + `class_destroy` + `cdev_del` + `unregister_chrdev_region` + `kfree`。 |

#### Runtime Error Path

| path | 錯誤條件 | 回傳 |
|---|---|---|
| `chardev_write` | `drv.read_only != 0` | `-EACCES` |
| `chardev_ioctl` | wrong `_IOC_TYPE` | `-ENOTTY` |
| `chardev_ioctl` | `_IOC_NR(cmd) > CHARDEV_MAGIC_MAX` | `-ENOTTY` |
| `IOCTL_GET_LEN` | `copy_to_user` failure | `-EFAULT` |
| `IOCTL_SET_RDONLY` | `copy_from_user` failure | `-EFAULT` |
| sysfs `read_only_store` | `kstrtoint` failure | `-EINVAL` |
| `chardev_read` | `*ppos >= drv.buf_len` | `0` EOF |

#### Script Error Behavior

`scripts/load.sh` 對 `insmod` 使用 `|| true`，因此即使 module load 失敗，script 仍會繼續檢查 device、chmod、modinfo 與 dmesg。這可能讓 script exit status 不能完全代表 module load 成功。這是 script 直接觀察，不是 driver 行為。

---

### 7. Synchronization Role

#### # Direct Observation

有 mutex 保護的 path：

- `chardev_read` 中的 copy_to_user 與 `*ppos` / `read_count` 更新。
- `chardev_write` 中的 copy_from_user、`buf_len`、`*ppos`、`write_count` 更新。
- `IOCTL_RESET_BUF` 中的 `memset` 與 `buf_len = 0`。

未受 mutex 保護但會讀/寫 shared state 的 path：

- `chardev_read` 在 mutex 外讀 `drv.buf_len` 做 EOF check。
- `chardev_write` 在 mutex 外讀 `drv.read_only`。
- `IOCTL_GET_LEN` 直接讀 `drv.buf_len`，沒有 mutex。
- `IOCTL_SET_RDONLY` 直接寫 `drv.read_only`，沒有 mutex。
- `read_only_show` / `read_only_store` 直接讀寫 `drv.read_only`，沒有 mutex。
- `buf_len_show` 直接讀 `drv.buf_len`，沒有 mutex。
- `proc_show` 直接讀 `drv.buf_len`、`drv.read_only`、`drv.buf`，沒有 mutex。

#### # Conservative Inference

因為所有 file descriptors 共享同一個 global `drv`，多個 process 同時 read/write/ioctl/sysfs 操作時會碰到同一份 state。mutex 已涵蓋主要 buffer copy/reset path，但 `read_only` 與部分 `buf_len` observation 沒有同步保護；在 32-bit int 讀寫通常是自然大小操作，但是否符合完整一致性需求無法只靠目前程式碼確認。可直接指出的是：proc/sysfs snapshot 可能讀到與 write/reset 交錯的狀態。

---

### 8. 比較分析

#### `copy_to_user` vs `copy_from_user`

- `copy_from_user` 出現在 write 與 `IOCTL_SET_RDONLY`，資料方向是 userspace 到 kernel。
- `copy_to_user` 出現在 read 與 `IOCTL_GET_LEN`，資料方向是 kernel 到 userspace。
- 使用原因可由 code 直接驗證：`write` 要接收 userspace buffer 存入 `drv.buf`；`read` 要把 `drv.buf` 回傳；`IOCTL_SET_RDONLY` 要接收 int；`IOCTL_GET_LEN` 要回傳 int。

#### `read_only` 控制：ioctl vs sysfs

| path | API | parsing/copy | state effect |
|---|---|---|---|
| ioctl | `IOCTL_SET_RDONLY` | `copy_from_user(&val, (int __user *)arg, sizeof(val))` | `drv.read_only = !!val` |
| sysfs | `read_only_store` | `kstrtoint(buf, 10, &val)` | `drv.read_only = !!val` |

兩者差異在外部介面與資料來源：ioctl 需要已開啟的 fd 與 userspace pointer；sysfs 透過文字輸入。使用原因只能依 code 說明：兩者都提供控制同一個 read-only flag 的入口，沒有看到額外權限或鎖定差異。

#### callback 機制差異

| callback 類型 | 註冊方式 | 觸發來源 | data model |
|---|---|---|---|
| VFS char device | `cdev_init(&drv.cdev, &chardev_fops)` | `/dev/chardev0` open/read/write/ioctl/close | 使用 file position `*ppos` + global `drv`。 |
| procfs | `proc_create(..., &chardev_proc_ops)` | `/proc/chardev_info` read | 使用 seq_file snapshot + global `drv`。 |
| sysfs | `DEVICE_ATTR_*` + `ATTRIBUTE_GROUPS` + `dev_groups` | `/sys/class/chardev/chardev0/*` read/write | 每個 attribute 對應 show/store + global `drv`。 |

#### dispatch model

此 driver 的 dispatch model 是 Linux framework-driven，不是 driver 自己輪詢：

- `/dev` path 由 VFS + cdev registry dispatch。
- `/proc` path 由 procfs + seq_file dispatch。
- `/sys` path 由 device model + generated attributes dispatch。

#### resource management model

相較於使用 `devm_*` 的 driver，這份 code 採完全手動 resource management。使用原因只能從 code 結構推得：這是 module-global char device，不是 platform device probe 生命週期，所以 source code 直接在 `chardev_init` 建立 resource，並在 `chardev_exit` / goto unwind 釋放。

---

### 9. Debug / Risk Analysis

#### Potential Memory Leak

- 正常 `chardev_exit` 有 `kfree(drv.buf)`，也有 `proc_remove`、`device_destroy`、`class_destroy`、`cdev_del`、`unregister_chrdev_region`。
- init failure path 有分階段 unwind，從 source 來看已覆蓋 `alloc_chrdev_region`、`cdev_add`、`class_create`、`device_create`、`proc_create` 後的失敗釋放。
- 目前程式碼中未觀察到其他 dynamic allocation 需要釋放。

#### Invalid Ownership Transfer

- `copy_to_user` / `copy_from_user` 都是 copy semantics，沒有把 `drv.buf` pointer 交給 userspace 保存。
- driver 不保存 userspace pointer；`arg` 只在 ioctl call 期間使用。
- `filp->private_data` 未使用，因此沒有 per-open ownership 或 release mismatch。

#### Callback Misuse Risk

- `proc_show` 使用 `seq_printf(m, "buf_content: %.*s\n", drv.buf_len, drv.buf)`，沒有 mutex。若同時有 write/reset，proc output 可能與更新交錯。
- sysfs `read_only_store` 與 ioctl `IOCTL_SET_RDONLY` 都能改 `drv.read_only`，但沒有共同 locking。若兩邊同時寫，最後狀態由最後一次寫入決定；程式碼中未提供 ordering 保證。
- `chardev_read` 的 EOF check 在 mutex 外，若同時 reset/write，`drv.buf_len` 可能在 check 與 locked copy 之間變化。這是從目前程式碼可見的同步範圍風險。

#### Lifecycle Mismatch

- `scripts/load.sh` 使用 `insmod ... || true`，module 載入失敗時 script 仍可能繼續顯示後續步驟；這可能讓 demo lifecycle 判讀混淆。
- `scripts/unload.sh` 直接 `sudo rmmod chardev`，沒有 `|| true`。如果 module 未載入，script 會因 rmmod 失敗而中止，後續驗證與 make clean 不會執行。
- `driver/Makefile` 的 `install` target 直接 `sudo insmod chardev.ko`，而 `scripts/load.sh` 先 make 再 insmod；兩者都可載入 module，但 error handling 不同。

#### Concurrency Issue

- `drv.buf` 的主要 read/write/reset path 使用 mutex，這是正面保護。
- `drv.read_only` 沒有 mutex 或 atomic 保護，且可由 ioctl 與 sysfs 兩條 path 修改。
- `drv.buf_len` 有些 path 在 mutex 內寫，但 proc/sysfs/ioctl GET_LEN 讀取時未加 mutex。
- atomic counters 適合計數本身，但 counters 與 buffer state 不是同一個一致性 snapshot；procfs 同時印 counters 與 buffer 時，無法保證是同一瞬間狀態。

#### Userspace Test / Build Risk

- `userspace/test_app.c:62` 目前可見內容是以 `/*` 開頭、但同一行顯示為 `?/` 而不是標準 `*/`。若檔案實際內容也是如此，這段註解可能未正確關閉，會影響 userspace test 編譯。此處未執行 build；只能根據目前讀到的 source 標示風險。
- `test_app.c` 在 `read()` 後直接 `rbuf[ret] = '\0'`。若 `read` 回傳負值，會用負 index 寫入；目前測試 path 預期成功，但程式碼沒有檢查 `ret < 0`。
- `test_app.c` 使用 `lseek(fd, 0, SEEK_SET)`，driver 沒有自訂 `.llseek`。目前無法從 driver source 確認所有 lseek 行為，僅能確認 userspace test 依賴它。

---

## 補充：Interface / State 對照

### ioctl Macro

| Macro | 定義位置 | command number | driver 行為 |
|---|---|---:|---|
| `CHARDEV_MAGIC` | `driver/chardev.h:7` | `'k'` | `chardev_ioctl` 用 `_IOC_TYPE(cmd)` 驗證。 |
| `IOCTL_RESET_BUF` | `driver/chardev.h:10` | 0 | 清空 `drv.buf`，`drv.buf_len = 0`。 |
| `IOCTL_GET_LEN` | `driver/chardev.h:11` | 1 | 回傳 `drv.buf_len` 到 userspace int。 |
| `IOCTL_SET_RDONLY` | `driver/chardev.h:12` | 2 | 從 userspace int 設定 `drv.read_only`。 |
| `CHARDEV_MAGIC_MAX` | `driver/chardev.h:14` | 2 | `chardev_ioctl` 拒絕 nr > 2。 |

### External Interfaces

| Interface | 建立來源 | callback / operation | 主要 state |
|---|---|---|---|
| `/dev/chardev0` | `device_create` + cdev registration | `chardev_fops` | `drv.buf`、`drv.buf_len`、`drv.read_only`、counters |
| `/proc/chardev_info` | `proc_create` | `chardev_proc_ops` -> `proc_show` | status snapshot |
| `/sys/class/chardev/chardev0/buf_len` | `DEVICE_ATTR_RO(buf_len)` | `buf_len_show` | `drv.buf_len` |
| `/sys/class/chardev/chardev0/read_only` | `DEVICE_ATTR_RW(read_only)` | `read_only_show/store` | `drv.read_only` |
| `/sys/class/chardev/chardev0/stats` | `DEVICE_ATTR_RO(stats)` | `stats_show` | atomic counters |

---

## 結論

`chardev-driver` 目前是一個 module-global state 的 character device demo。它的核心 execution semantics 是：module load 時一次性配置 `drv.buf`、註冊 dev_t/cdev/class/device/proc entry；runtime 由 VFS、procfs、sysfs 三種 framework 間接 dispatch 到各自 callback；所有外部介面都讀寫同一個 global `drv` 狀態。

ownership/lifecycle 上，此專案沒有 devm-managed resource，而是完全手動註冊與反註冊。正常 exit 與 init failure unwind 都可追溯到對應 cleanup。主要風險集中在 shared state 的同步範圍：buffer copy/reset 有 mutex，但 `read_only`、部分 `buf_len` 讀取與 proc/sysfs snapshot 沒有同一把 lock 保護；另外 userspace test 目前可見註解疑似未正確關閉，且 read error handling 不完整。
