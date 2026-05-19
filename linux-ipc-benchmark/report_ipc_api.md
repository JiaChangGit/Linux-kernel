# Linux IPC Benchmark API / Architecture 技術分析報告

本報告只依據 `/linux-ipc-benchmark` 目前實際存在的 source code、header、Makefile 與 scripts 進行分析。以下內容以既有 report 的「Codebase Trace」與「Architecture / API Technical Report」結構為基礎補強，避免套用外部設計假設。

## 分析標記

- `# Direct Observation`：可由目前 codebase 直接驗證。
- `# Conservative Inference`：只依據呼叫關係或非常保守的執行語意推論，並明確標示。
- 無法從目前內容確認的地方，會標示「目前程式碼中未觀察到」或「無法從現有內容確認」。

---

## 第一階段：Codebase Trace

### 1. Project Structure

# Direct Observation

| 類別 | 檔案 | 角色 |
| --- | --- | --- |
| Kernel source | `kernel/mq_module.c` | 實作 `/dev/mq_ipc` char device，使用 `kfifo`、`mutex`、`wait_queue` 作為 Message Queue IPC。 |
| Kernel source | `kernel/shm_module.c` | 實作 `/dev/shm_ipc` char device，使用 `vmalloc` ring buffer，提供 syscall read/write 與 `mmap` 共享記憶體路徑。 |
| Kernel build | `kernel/Makefile` | 建置 `mq_module.ko` 與 `shm_module.ko`。 |
| User source | `user/benchmark.c` | 同時測試 MQ syscall、SHM syscall、SHM mmap 三條 runtime path。 |
| User source | `user/mq_demo.c` | 對 `/dev/mq_ipc` 做小量 write/read demo。 |
| User source | `user/shm_demo.c` | 對 `/dev/shm_ipc` 做 `mmap` ring buffer demo。 |
| Header | `user/common.h` | 定義 user space 共用常數、device path、`shm_region_t` layout 與 `now_us()`。 |
| User build | `user/Makefile` | 建置 `benchmark`、`mq_demo`、`shm_demo`。 |
| Root build | `Makefile` | 提供 `kernel`、`user`、`clean` 等目標。 |
| Script | `scripts/01_setup.sh` | 安裝依賴、建置 kernel/user、`insmod` modules、設定 device 權限。 |
| Script | `scripts/02_demo.sh` | 檢查 device，執行 `mq_demo` 與 `shm_demo`，讀取 `/proc/*_stats`。 |
| Script | `scripts/03_benchmark.sh` | 檢查 device 與 binary，執行 `user/benchmark`。 |
| Script | `scripts/04_cleanup.sh` | `rmmod` modules、清理 build artifact、檢查 device/proc/module 是否移除。 |

#### Module / Component Relationship

# Direct Observation

```text
scripts/01_setup.sh
  -> make kernel/user
  -> insmod kernel/mq_module.ko
       -> mq_init()
       -> /dev/mq_ipc + /proc/mq_stats
  -> insmod kernel/shm_module.ko
       -> shm_init()
       -> /dev/shm_ipc + /proc/shm_stats

user/mq_demo.c / user/benchmark.c
  -> open("/dev/mq_ipc")
  -> write/read syscall
  -> kernel/mq_module.c:g_fops

user/shm_demo.c / user/benchmark.c
  -> open("/dev/shm_ipc")
  -> read/write syscall OR mmap()
  -> kernel/shm_module.c:g_fops
```

# Conservative Inference

`linux-ipc-benchmark` 的比較軸線是「同樣 64 bytes message」在三種 path 中的成本差異：MQ syscall、SHM syscall、SHM mmap。這個推論來自 `user/benchmark.c` 實際依序呼叫三種測試，而不是 README 敘述假設。

---

### 2. Semantic Element Extraction（只列實際存在）

#### API / External Interface

# Direct Observation

- `/dev/mq_ipc`
  - 定義：`kernel/mq_module.c` 的 `MQ_DEVICE "mq_ipc"`。
  - 建立：`mq_init()` 透過 `alloc_chrdev_region()`、`cdev_add()`、`class_create()`、`device_create()`。
  - 使用：`user/mq_demo.c` 與 `user/benchmark.c` 透過 `open()`、`write()`、`read()`。

- `/dev/shm_ipc`
  - 定義：`kernel/shm_module.c` 的 `SHM_DEVICE "shm_ipc"`。
  - 建立：`shm_init()` 透過 `alloc_chrdev_region()`、`cdev_add()`、`class_create()`、`device_create()`。
  - 使用：`user/shm_demo.c` 與 `user/benchmark.c` 透過 `open()`、`write()`、`read()`、`mmap()`。

- `/proc/mq_stats`
  - 建立：`mq_init()` 呼叫 `proc_create("mq_stats", 0444, NULL, &g_proc_ops)`。
  - callback：`mq_proc_open()` -> `single_open()` -> `mq_stats_show()`。

- `/proc/shm_stats`
  - 建立：`shm_init()` 呼叫 `proc_create("shm_stats", 0444, NULL, &g_proc_ops)`。
  - callback：`shm_proc_open()` -> `single_open()` -> `shm_stats_show()`。

#### Macro

# Direct Observation

| Macro | 位置 | 用途 |
| --- | --- | --- |
| `MQ_DEVICE` | `kernel/mq_module.c` | char device 名稱 `mq_ipc`。 |
| `MSG_SIZE` | `kernel/mq_module.c`、`kernel/shm_module.c`、`user/common.h` | 固定 message slot 大小 64 bytes。 |
| `QUEUE_DEPTH` | `kernel/mq_module.c` | MQ FIFO slot 數 512。 |
| `FIFO_SIZE` | `kernel/mq_module.c` | `MSG_SIZE * QUEUE_DEPTH`，供 `DEFINE_KFIFO` 使用。 |
| `SHM_DEVICE` | `kernel/shm_module.c` | char device 名稱 `shm_ipc`。 |
| `RING_CAPACITY` | `kernel/shm_module.c`、`user/common.h` | shared memory ring slot 數 512。 |
| `SHM_BUF_SIZE` | `kernel/shm_module.c` | `PAGE_ALIGN(sizeof(struct shm_region))`，供 `vmalloc()` 與 `mmap()` size 檢查使用。 |
| `SHM_MAP_SIZE` | `user/common.h` | user space `mmap()` 長度，對齊 `sizeof(shm_region_t)` 到 page 邊界。 |
| `DEFAULT_COUNT` | `user/benchmark.c` | benchmark 預設 message count 200000。 |
| `DEMO_N` | `user/mq_demo.c`、`user/shm_demo.c` | demo message 數量 8。 |

#### Inline Function

# Direct Observation

- `now_us()`
  - 位置：`user/common.h`。
  - 類型：`static inline double`。
  - 使用：`user/benchmark.c` 的 `syscall_worker()`、`mmap_worker()`、`run_test()`。
  - 行為：呼叫 `clock_gettime(CLOCK_MONOTONIC, &ts)`，回傳 microseconds。

#### Callback / Function Pointer / Operation Table

# Direct Observation

| 名稱 | 類型 | 位置 | 綁定內容 |
| --- | --- | --- | --- |
| `g_fops` | `struct file_operations` | `kernel/mq_module.c` | `.open = mq_open`、`.release = mq_release`、`.write = mq_write`、`.read = mq_read`。 |
| `g_proc_ops` | `struct proc_ops` | `kernel/mq_module.c` | `.proc_open = mq_proc_open`、`.proc_read = seq_read`、`.proc_lseek = seq_lseek`、`.proc_release = single_release`。 |
| `g_fops` | `struct file_operations` | `kernel/shm_module.c` | `.open = shm_open`、`.release = shm_release`、`.write = shm_write`、`.read = shm_read`、`.mmap = shm_mmap`。 |
| `g_proc_ops` | `struct proc_ops` | `kernel/shm_module.c` | `.proc_open = shm_proc_open`、`.proc_read = seq_read`、`.proc_lseek = seq_lseek`、`.proc_release = single_release`。 |
| `syscall_worker` | pthread worker callback | `user/benchmark.c` | 傳給 `pthread_create()`，測試 `read/write` syscall path。 |
| `mmap_worker` | pthread worker callback | `user/benchmark.c` | 傳給 `pthread_create()`，測試 user-space mmap ring path。 |

#### Linker / Compiler Annotation

# Direct Observation

- `module_init(mq_init)`、`module_exit(mq_exit)`：`kernel/mq_module.c`。
- `module_init(shm_init)`、`module_exit(shm_exit)`：`kernel/shm_module.c`。
- `MODULE_LICENSE("GPL")`、`MODULE_AUTHOR(...)`、`MODULE_DESCRIPTION(...)`：兩個 kernel module 皆有。
- `volatile`：`struct shm_region` 與 `shm_region_t` 的 `head`、`tail` 欄位。

目前程式碼中未觀察到 custom linker script、section attribute、`__init`、`__exit`、`__attribute__`。

#### Conditional Compilation

# Direct Observation

目前 `kernel/*.c`、`user/*.c`、`user/common.h` 中未觀察到 `#ifdef` / `#if` 條件編譯控制主要執行路徑。

#### Synchronization Primitive

# Direct Observation

- MQ kernel path：
  - `DEFINE_MUTEX(g_lock)`：保護 `g_fifo` 的 `kfifo_in()` / `kfifo_out()`。
  - `DECLARE_WAIT_QUEUE_HEAD(g_rd_wq)`：reader 等待 FIFO 有資料。
  - `DECLARE_WAIT_QUEUE_HEAD(g_wr_wq)`：writer 等待 FIFO 有空間。
  - `atomic64_t`：累積 enqueue/dequeue 與 latency 統計。
  - `READ_ONCE()` / `WRITE_ONCE()`：存取 `st_last_enq_ts`。

- SHM kernel syscall path：
  - `spinlock_t g_spin`：保護 `g_shm->head/tail/data` 的 read/write syscall path。
  - `smp_wmb()`：writer 更新 `head` 前的 memory ordering。
  - `smp_rmb()`：reader 從 ring slot 複製資料前的 memory ordering。
  - `atomic64_t`：累積 write/read 與 latency 統計。
  - `READ_ONCE()` / `WRITE_ONCE()`：存取 `st_last_wr_ts`。

- User benchmark/demo mmap path：
  - `pthread_barrier_t`：benchmark 中讓 producer/consumer 同步起跑。
  - `__sync_synchronize()`：user-space ring buffer 的 memory barrier。

目前程式碼中未觀察到 futex、semaphore、rwlock、completion、workqueue、kernel thread。

#### Memory Management Mechanism

# Direct Observation

- MQ：
  - `DEFINE_KFIFO(g_fifo, char, FIFO_SIZE)`：static kernel FIFO buffer。
  - `char kb[MSG_SIZE]`：`mq_write()` stack buffer。
  - `char kb[MSG_SIZE]`：`mq_read()` stack buffer。
  - `copy_from_user()` / `copy_to_user()`：user/kernel copy boundary。

- SHM：
  - `vmalloc(SHM_BUF_SIZE)`：配置 kernel ring buffer。
  - `vfree(g_shm)`：釋放 kernel ring buffer。
  - `mmap()` / `remap_pfn_range()` / `vmalloc_to_pfn()`：把 `vmalloc` backing pages 映射到 user VMA。
  - `memcpy()`：kernel read path 與 user mmap path 使用固定 64 bytes slot copy。

#### Execution Model / Event Dispatch

# Direct Observation

- Kernel char device dispatch：VFS 根據 `struct file_operations` dispatch `open/read/write/mmap/release`。
- Procfs dispatch：procfs 根據 `struct proc_ops` dispatch `open/read/lseek/release`。
- User benchmark dispatch：`run_test()` 依 `use_mmap` 選擇把 `mmap_worker` 或 `syscall_worker` 傳給 `pthread_create()`。

#### Communication Mechanism

# Direct Observation

- MQ path：user `write()` -> kernel `kfifo_in()`；user `read()` -> kernel `kfifo_out()`。
- SHM syscall path：user `write()` / `read()` 透過 `/dev/shm_ipc` 進入 kernel ring buffer。
- SHM mmap path：user 直接讀寫 `shm_region_t` 的 `head/tail/data`，不經每筆 message syscall。

#### Registration Mechanism

# Direct Observation

- char device registration：`alloc_chrdev_region()`、`cdev_init()`、`cdev_add()`、`class_create()`、`device_create()`。
- proc registration：`proc_create()`。
- module registration：`module_init()` / `module_exit()`。

目前程式碼中未觀察到 ioctl command registration、netlink family、sysfs attribute、miscdevice、platform_driver registration。

---

### 3. API / Macro Inventory（分類整理）

#### Initialization

# Direct Observation

| 名稱 | 類型 | 定義位置 | 呼叫位置 / 來源 | 用途 | 關聯 data | Flow 影響 |
| --- | --- | --- | --- | --- | --- | --- |
| `mq_init` | module init function | `kernel/mq_module.c` | `module_init(mq_init)`，由 `insmod mq_module.ko` 觸發 | 初始化 MQ char device 與 proc stats | `g_fifo`、`g_lock`、`g_devno`、`g_cdev`、`g_class`、atomic stats | 建立 `/dev/mq_ipc` 與 `/proc/mq_stats`，使 MQ runtime path 可被 VFS/procfs dispatch。 |
| `shm_init` | module init function | `kernel/shm_module.c` | `module_init(shm_init)`，由 `insmod shm_module.ko` 觸發 | 配置 SHM ring、初始化 char device 與 proc stats | `g_shm`、`g_spin`、`g_devno`、`g_cdev`、`g_class`、atomic stats | 建立 `/dev/shm_ipc`、`/proc/shm_stats`，並提供 mmap backing buffer。 |
| `spin_lock_init(&g_spin)` | init API | `kernel/shm_module.c` | `shm_init()` | 初始化 SHM syscall path lock | `g_spin` | `shm_read()` / `shm_write()` 之後可保護 ring state。 |
| `pthread_barrier_init` | pthread API | `user/benchmark.c` | `run_test()` | 讓 producer/consumer 同步起跑 | `pthread_barrier_t bar` | 減少 benchmark thread 起跑時間差。 |
| `now_us()` | inline helper | `user/common.h` | `syscall_worker()`、`mmap_worker()`、`run_test()` | 量測 elapsed time | `struct timespec` | 影響 benchmark 統計，不影響 IPC data path。 |

#### Registration

# Direct Observation

| 名稱 | 類型 | 定義 / 呼叫位置 | 呼叫來源 | 用途 | 關聯 data | Flow 影響 |
| --- | --- | --- | --- | --- | --- | --- |
| `alloc_chrdev_region` | kernel API | `mq_init()`、`shm_init()` | module init | 配置 device number | `g_devno` | 後續 `cdev_add()` 與 `device_create()` 依賴此值。 |
| `cdev_init` | kernel API | `mq_init()`、`shm_init()` | module init | 將 `g_cdev` 綁定到 `g_fops` | `g_cdev`、`g_fops` | 建立 VFS indirect dispatch table。 |
| `cdev_add` | kernel API | `mq_init()`、`shm_init()` | module init | 註冊 char device | `g_cdev`、`g_devno` | 讓 device operation 可被核心呼叫。 |
| `class_create` | kernel API | `mq_init()`、`shm_init()` | module init | 建立 device class | `g_class` | `device_create()` 依賴。 |
| `device_create` | kernel API | `mq_init()`、`shm_init()` | module init | 建立 `/dev/mq_ipc` 或 `/dev/shm_ipc` | `g_class`、`g_devno` | 對 user space 暴露 external interface。 |
| `proc_create` | kernel API | `mq_init()`、`shm_init()` | module init | 建立 `/proc/mq_stats` 或 `/proc/shm_stats` | `g_proc_ops` | 提供 stats read callback chain。 |
| `module_init` / `module_exit` | kernel macro | 兩個 module 檔案尾端 | kernel module loader | 綁定載入/卸載 entry | `mq_init/mq_exit`、`shm_init/shm_exit` | 決定 init/cleanup chain。 |

#### Execution Path

# Direct Observation

| 名稱 | 類型 | 定義位置 | 呼叫來源 | 用途 | 關聯 data | Flow 影響 |
| --- | --- | --- | --- | --- | --- | --- |
| `mq_write` | `file_operations.write` callback | `kernel/mq_module.c` | user `write(fd, buf, MSG_SIZE)` | 將 user buffer 複製進 `g_fifo` | `g_fifo`、`g_lock`、`g_wr_wq`、`g_rd_wq` | 若 FIFO 空間不足會 sleep；成功後喚醒 reader。 |
| `mq_read` | `file_operations.read` callback | `kernel/mq_module.c` | user `read(fd, buf, MSG_SIZE)` | 從 `g_fifo` 複製 64 bytes 到 user | `g_fifo`、`g_lock`、`g_rd_wq`、`g_wr_wq` | 若 FIFO 無資料會依 blocking/nonblocking 行為等待或回錯。 |
| `shm_write` | `file_operations.write` callback | `kernel/shm_module.c` | user `write(fd, buf, MSG_SIZE)` | 透過 syscall 將 user data 寫入 shared ring | `g_shm->head/tail/data`、`g_spin` | ring full 時回 `-ENOSPC`；成功更新 `head`。 |
| `shm_read` | `file_operations.read` callback | `kernel/shm_module.c` | user `read(fd, buf, MSG_SIZE)` | 透過 syscall 從 shared ring 讀出 data | `g_shm->head/tail/data`、`g_spin` | ring empty 時回 `-EAGAIN`；成功更新 `tail`。 |
| `shm_mmap` | `file_operations.mmap` callback | `kernel/shm_module.c` | user `mmap(fd)` | 將 `g_shm` backing pages 映射給 user | `g_shm`、`SHM_BUF_SIZE`、VMA | 建立 zero-copy runtime path 的 memory sharing。 |
| `syscall_worker` | pthread worker | `user/benchmark.c` | `pthread_create()` | producer/consumer 透過 `write/read` 反覆傳訊 | `targ_t`、device fd | 執行 MQ syscall 或 SHM syscall benchmark。 |
| `mmap_worker` | pthread worker | `user/benchmark.c` | `pthread_create()` | producer/consumer 直接操作 mmap ring | `targ_t`、`shm_region_t *` | 執行 SHM mmap benchmark。 |

#### Lifecycle / Cleanup

# Direct Observation

| 名稱 | 類型 | 定義位置 | 呼叫來源 | 用途 | 關聯 data | Flow 影響 |
| --- | --- | --- | --- | --- | --- | --- |
| `mq_exit` | module exit function | `kernel/mq_module.c` | `rmmod mq_module` | 移除 proc、device、class、cdev、devno | `/proc/mq_stats`、`g_class`、`g_cdev`、`g_devno` | 結束 MQ external interface。 |
| `shm_exit` | module exit function | `kernel/shm_module.c` | `rmmod shm_module` | 移除 proc、device、class、cdev、devno，釋放 `g_shm` | `g_shm`、`g_class`、`g_cdev`、`g_devno` | 結束 SHM interface 並釋放 backing memory。 |
| `munmap` | libc API | `user/benchmark.c`、`user/shm_demo.c` | user cleanup | 解除 user VMA | `shm_region_t *shm` | 結束 mmap path 的 user mapping。 |
| `close` | libc API | user programs | user cleanup | 關閉 device fd | `mqfd`、`shmfd` | 觸發 kernel `.release` callback，但目前 release 只回 0。 |
| `scripts/04_cleanup.sh` | shell script | `scripts/04_cleanup.sh` | 手動執行 | 先卸載 `shm_module`，再卸載 `mq_module`，最後 `make clean` | kernel modules、build artifacts | 清理 runtime 與 build state。 |

#### Memory Handling

# Direct Observation

| 名稱 | 類型 | 位置 | 呼叫來源 | 用途 | 關聯 data | Flow 影響 |
| --- | --- | --- | --- | --- | --- | --- |
| `DEFINE_KFIFO` | kernel macro | `kernel/mq_module.c` | static definition | 配置 MQ FIFO | `g_fifo` | MQ data queue 的 storage。 |
| `kfifo_in` | kernel API | `mq_write()` | write callback | 寫入固定 64 bytes slot | `g_fifo` | 增加 FIFO data length。 |
| `kfifo_out` | kernel API | `mq_read()` | read callback | 讀出固定 64 bytes slot | `g_fifo` | 減少 FIFO data length。 |
| `vmalloc` | kernel API | `shm_init()` | module init | 配置 shared ring backing memory | `g_shm` | SHM syscall/mmap path 共用 storage。 |
| `vfree` | kernel API | `shm_exit()` / init unwind | module cleanup | 釋放 `g_shm` | `g_shm` | 結束 shared ring lifetime。 |
| `copy_from_user` | kernel API | `mq_write()`、`shm_write()` | write callback | 從 user buffer 複製到 kernel | user pointer、kernel buffer | user/kernel boundary；失敗回 `-EFAULT`。 |
| `copy_to_user` | kernel API | `mq_read()`、`shm_read()` | read callback | 從 kernel 複製到 user buffer | kernel buffer、user pointer | user/kernel boundary；失敗回 `-EFAULT`。 |
| `remap_pfn_range` | kernel API | `shm_mmap()` | mmap callback | 將 page frame 映射到 VMA | `g_shm`、VMA | 建立 user-space direct access。 |

#### Synchronization

# Direct Observation

| 名稱 | 類型 | 位置 | 呼叫來源 | 用途 | 關聯 data | Flow 影響 |
| --- | --- | --- | --- | --- | --- | --- |
| `g_lock` | `struct mutex` | `kernel/mq_module.c` | `mq_write()`、`mq_read()` | 保護 `kfifo_in/out` | `g_fifo` | 防止 MQ FIFO 修改互相交錯。 |
| `g_rd_wq` | wait queue | `kernel/mq_module.c` | `mq_read()` wait、`mq_write()` wake | reader 等待資料 | `g_fifo` | MQ read blocking semantics。 |
| `g_wr_wq` | wait queue | `kernel/mq_module.c` | `mq_write()` wait、`mq_read()` wake | writer 等待空間 | `g_fifo` | MQ write blocking semantics。 |
| `g_spin` | `spinlock_t` | `kernel/shm_module.c` | `shm_write()`、`shm_read()` | 保護 syscall path ring state | `g_shm` | SHM syscall path 不 sleep 等待，只回 error。 |
| `smp_wmb` / `smp_rmb` | kernel barrier | `kernel/shm_module.c` | `shm_write()`、`shm_read()` | 控制 ring data/head/tail 可見性 | `g_shm->data/head/tail` | 降低 reader 看見 head 但 data 未完成的風險。 |
| `__sync_synchronize` | compiler builtin | `user/benchmark.c`、`user/shm_demo.c` | mmap producer/consumer | user-space memory barrier | `shm_region_t` | 控制 mmap ring data/head/tail 可見性。 |
| `pthread_barrier_wait` | pthread API | `user/benchmark.c` | worker start | 同步 producer/consumer 開始時間 | `targ_t.bar` | 影響 benchmark timing。 |

#### Event Dispatch

# Direct Observation

| 名稱 | 類型 | 位置 | 呼叫來源 | 用途 | Flow 影響 |
| --- | --- | --- | --- | --- | --- |
| `struct file_operations g_fops` | operation table | 兩個 kernel module | VFS | dispatch device operations | user syscall 會間接進入 module callbacks。 |
| `struct proc_ops g_proc_ops` | operation table | 兩個 kernel module | procfs | dispatch proc read operations | `cat /proc/*_stats` 會間接進入 stats callbacks。 |
| `pthread_create(..., syscall_worker/mmap_worker, ...)` | callback dispatch | `user/benchmark.c` | `run_test()` | 選擇 benchmark worker | 決定 runtime path 是 syscall 或 mmap。 |

#### Logging / Debug

# Direct Observation

- Kernel：
  - `pr_info()`：module init/exit 成功訊息。
  - `pr_err()`：init 失敗 unwind 相關錯誤訊息。
  - `seq_printf()`：`mq_stats_show()` 與 `shm_stats_show()` 輸出 proc stats。

- User / script：
  - `printf()` / `puts()`：demo 與 benchmark 結果。
  - `perror()`：user program open/mmap error。
  - `system("cat /proc/mq_stats")`、`system("cat /proc/shm_stats")`：`user/benchmark.c` 與 demo 直接讀 proc stats。

#### Error Handling

# Direct Observation

| Error | 來源 | 語意 |
| --- | --- | --- |
| `-EFAULT` | `copy_from_user()` / `copy_to_user()` failure | user pointer copy 失敗。 |
| `-EINTR` | `mq_write()` / `mq_read()` wait interrupted | blocking wait 被 signal 中斷。 |
| `-EAGAIN` | `mq_read()` nonblock empty、`mq_read()` unexpected FIFO short read、`shm_read()` empty | 暫時不可讀或狀態不符合。 |
| `-ENOSPC` | `shm_write()` ring full | syscall SHM ring 滿。 |
| `-EINVAL` | read length < `MSG_SIZE`、`shm_mmap()` VMA size 過大 | caller 參數不符合固定 64 bytes / mapping size 要求。 |
| `-ENOMEM` | `vmalloc()` failure、device/class error path | kernel resource allocation failure。 |

# Conservative Inference

`user/benchmark.c` 的 retry loop 只針對 `EAGAIN`、`ENOSPC`、`EINTR` 重試；其他錯誤會跳出 loop，但 worker 沒有把錯誤傳回 `main()`。因此 benchmark 結果可能在 error path 下仍繼續統計，這是從目前控制流程推論出的風險。

---

### 4. Call Graph

#### Initialization Chain

# Direct Observation

```text
scripts/01_setup.sh
  -> apt-get update/install dependencies
  -> make -C "${PROJECT_DIR}" kernel
  -> make -C "${PROJECT_DIR}" user
  -> insmod kernel/mq_module.ko
       -> module_init(mq_init)
       -> atomic64_set(stats)
       -> alloc_chrdev_region(&g_devno)
       -> cdev_init(&g_cdev, &g_fops)
       -> cdev_add(&g_cdev, g_devno, 1)
       -> class_create(MQ_DEVICE)
       -> device_create(..., "mq_ipc")
       -> proc_create("mq_stats", 0444, NULL, &g_proc_ops)
  -> insmod kernel/shm_module.ko
       -> module_init(shm_init)
       -> g_shm = vmalloc(SHM_BUF_SIZE)
       -> memset(g_shm, 0, SHM_BUF_SIZE)
       -> g_shm->capacity = RING_CAPACITY
       -> g_shm->msg_size = MSG_SIZE
       -> spin_lock_init(&g_spin)
       -> atomic64_set(stats)
       -> alloc_chrdev_region(&g_devno)
       -> cdev_init(&g_cdev, &g_fops)
       -> cdev_add(&g_cdev, g_devno, 1)
       -> class_create(SHM_DEVICE)
       -> device_create(..., "shm_ipc")
       -> proc_create("shm_stats", 0444, NULL, &g_proc_ops)
  -> chmod 666 /dev/mq_ipc /dev/shm_ipc
```

#### Runtime Chain：MQ syscall path

# Direct Observation

```text
user producer
  -> write(mqfd, buf, MSG_SIZE)
  -> VFS dispatch: g_fops.write
  -> mq_write()
       -> clamp len to MSG_SIZE
       -> copy_from_user(kb, ubuf, len)
       -> wait_event_interruptible(g_wr_wq, kfifo_avail(&g_fifo) >= MSG_SIZE)
       -> mutex_lock(&g_lock)
       -> kfifo_in(&g_fifo, kb, MSG_SIZE)
       -> mutex_unlock(&g_lock)
       -> atomic64_inc(&st_enq)
       -> WRITE_ONCE(st_last_enq_ts, ts)
       -> wake_up_interruptible(&g_rd_wq)
       -> return MSG_SIZE

user consumer
  -> read(mqfd, buf, MSG_SIZE)
  -> VFS dispatch: g_fops.read
  -> mq_read()
       -> reject len < MSG_SIZE
       -> if nonblock and FIFO short: return -EAGAIN
       -> wait_event_interruptible(g_rd_wq, kfifo_len(&g_fifo) >= MSG_SIZE)
       -> mutex_lock(&g_lock)
       -> kfifo_out(&g_fifo, kb, MSG_SIZE)
       -> mutex_unlock(&g_lock)
       -> copy_to_user(ubuf, kb, MSG_SIZE)
       -> update latency counters
       -> wake_up_interruptible(&g_wr_wq)
       -> return MSG_SIZE
```

#### Runtime Chain：SHM syscall path

# Direct Observation

```text
user producer
  -> write(shmfd, buf, MSG_SIZE)
  -> VFS dispatch: g_fops.write
  -> shm_write()
       -> clamp len to MSG_SIZE
       -> spin_lock(&g_spin)
       -> head = g_shm->head
       -> next = (head + 1) % capacity
       -> if next == tail: return -ENOSPC
       -> copy_from_user(g_shm->data[head], ubuf, len)
       -> smp_wmb()
       -> g_shm->head = next
       -> spin_unlock(&g_spin)
       -> update stats
       -> return MSG_SIZE

user consumer
  -> read(shmfd, buf, MSG_SIZE)
  -> VFS dispatch: g_fops.read
  -> shm_read()
       -> reject len < MSG_SIZE
       -> spin_lock(&g_spin)
       -> tail = g_shm->tail
       -> if tail == head: return -EAGAIN
       -> smp_rmb()
       -> memcpy(tmp, g_shm->data[tail], MSG_SIZE)
       -> g_shm->tail = (tail + 1) % capacity
       -> spin_unlock(&g_spin)
       -> copy_to_user(ubuf, tmp, MSG_SIZE)
       -> update stats
       -> return MSG_SIZE
```

#### Runtime Chain：SHM mmap path

# Direct Observation

```text
setup
  -> user mmap(NULL, SHM_MAP_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0)
  -> VFS dispatch: g_fops.mmap
  -> shm_mmap()
       -> reject mapping larger than SHM_BUF_SIZE
       -> vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP)
       -> for each page:
            pfn = vmalloc_to_pfn(g_shm + offset)
            remap_pfn_range(vma, user_addr, pfn, PAGE_SIZE, vma->vm_page_prot)
       -> return 0

runtime producer
  -> mmap_worker()
       -> while ring full: spin
       -> memcpy(shm->data[head], fill, MSG_SIZE)
       -> __sync_synchronize()
       -> shm->head = next

runtime consumer
  -> mmap_worker()
       -> while ring empty: spin
       -> __sync_synchronize()
       -> memcpy(sink, shm->data[tail], MSG_SIZE)
       -> shm->tail = (tail + 1) % capacity
```

#### Cleanup Chain

# Direct Observation

```text
scripts/04_cleanup.sh
  -> if lsmod has shm_module: rmmod shm_module
       -> module_exit(shm_exit)
       -> remove_proc_entry("shm_stats", NULL)
       -> device_destroy(g_class, g_devno)
       -> class_destroy(g_class)
       -> cdev_del(&g_cdev)
       -> unregister_chrdev_region(g_devno, 1)
       -> vfree(g_shm)
  -> if lsmod has mq_module: rmmod mq_module
       -> module_exit(mq_exit)
       -> remove_proc_entry("mq_stats", NULL)
       -> device_destroy(g_class, g_devno)
       -> class_destroy(g_class)
       -> cdev_del(&g_cdev)
       -> unregister_chrdev_region(g_devno, 1)
  -> make clean
  -> verify lsmod, /dev, /proc cleanup
```

#### Callback Chain

# Direct Observation

```text
VFS open/read/write/release on /dev/mq_ipc
  -> mq_module.c:g_fops
  -> mq_open / mq_read / mq_write / mq_release

VFS open/read/write/mmap/release on /dev/shm_ipc
  -> shm_module.c:g_fops
  -> shm_open / shm_read / shm_write / shm_mmap / shm_release

procfs open/read on /proc/mq_stats
  -> mq_module.c:g_proc_ops.proc_open
  -> mq_proc_open()
  -> single_open(..., mq_stats_show, NULL)
  -> seq_read()
  -> mq_stats_show()

procfs open/read on /proc/shm_stats
  -> shm_module.c:g_proc_ops.proc_open
  -> shm_proc_open()
  -> single_open(..., shm_stats_show, NULL)
  -> seq_read()
  -> shm_stats_show()

pthread_create in benchmark
  -> syscall_worker OR mmap_worker
```

#### Indirect Call Chain / Dispatch Table

# Direct Observation

| Dispatch table | Function pointer | Bound function | Trigger |
| --- | --- | --- | --- |
| `mq_module.c:g_fops` | `.write` | `mq_write` | `write("/dev/mq_ipc")` |
| `mq_module.c:g_fops` | `.read` | `mq_read` | `read("/dev/mq_ipc")` |
| `mq_module.c:g_proc_ops` | `.proc_open` | `mq_proc_open` | `open("/proc/mq_stats")` |
| `shm_module.c:g_fops` | `.write` | `shm_write` | `write("/dev/shm_ipc")` |
| `shm_module.c:g_fops` | `.read` | `shm_read` | `read("/dev/shm_ipc")` |
| `shm_module.c:g_fops` | `.mmap` | `shm_mmap` | `mmap("/dev/shm_ipc")` |
| `shm_module.c:g_proc_ops` | `.proc_open` | `shm_proc_open` | `open("/proc/shm_stats")` |
| `pthread_create` | start routine | `syscall_worker` / `mmap_worker` | `run_test()` |

目前程式碼中未觀察到 ioctl dispatch table、poll table、netlink callback、timer callback、IRQ callback。

---

### 5. Struct / Resource Tracing

#### `struct shm_region`

# Direct Observation

- 定義位置：`kernel/shm_module.c`。
- 欄位：
  - `alignas(64) volatile uint32_t head`
  - `uint8_t pad1[60]`
  - `alignas(64) volatile uint32_t tail`
  - `uint8_t pad2[60]`
  - `uint32_t capacity`
  - `uint32_t msg_size`
  - `char data[RING_CAPACITY][MSG_SIZE]`
- allocation / init：
  - `shm_init()` 呼叫 `vmalloc(SHM_BUF_SIZE)`。
  - `memset(g_shm, 0, SHM_BUF_SIZE)`。
  - `g_shm->capacity = RING_CAPACITY`。
  - `g_shm->msg_size = MSG_SIZE`。
- ownership：
  - kernel module owns `g_shm` allocation。
  - user mmap path obtains mapping，未擁有 backing allocation。
- lifetime：
  - start：`shm_init()` 成功配置。
  - runtime：`shm_write()` / `shm_read()` / `shm_mmap()` / user mmap worker 共用。
  - release：`shm_exit()` 呼叫 `vfree(g_shm)`。
- state transition：
  - producer 更新 `head`。
  - consumer 更新 `tail`。
  - `(head + 1) % capacity == tail` 表示 full。
  - `tail == head` 表示 empty。
- data passing path：
  - syscall path：user buffer -> `copy_from_user()` -> `g_shm->data[head]` -> `memcpy(tmp, slot)` -> `copy_to_user()`。
  - mmap path：user producer `memcpy()` 直接寫 `shm->data[head]`，user consumer `memcpy()` 直接讀 `shm->data[tail]`。
- callback binding：
  - `shm_mmap()` 透過 `g_fops.mmap` 讓 `g_shm` 被映射。

#### `shm_region_t`

# Direct Observation

- 定義位置：`user/common.h`。
- layout 與 kernel `struct shm_region` 對應，包含 `head/tail/capacity/msg_size/_pad/data`。
- 使用位置：
  - `user/benchmark.c`：`mmap()` 後轉型為 `shm_region_t *`，由 `mmap_worker()` 使用。
  - `user/shm_demo.c`：`mmap()` 後重設 `head/tail` 並直接讀寫 ring。

# Conservative Inference

kernel 與 user 兩邊以相同欄位順序手動維持 ABI layout；目前程式碼中未觀察到 `static_assert`、版本欄位或 ioctl 查詢 layout 的機制。因此若其中一邊 struct layout 修改但另一邊未同步，mmap path 會有 ABI mismatch 風險。

#### MQ FIFO Resource：`g_fifo`

# Direct Observation

- 定義位置：`kernel/mq_module.c`。
- allocation / init：`DEFINE_KFIFO(g_fifo, char, FIFO_SIZE)` static allocation。
- ownership：MQ kernel module owns FIFO。
- lifetime：
  - module load 後存在。
  - module unload 後隨 module memory 消失。
- state transition：
  - `mq_write()` 以 `kfifo_in(&g_fifo, kb, MSG_SIZE)` 增加資料。
  - `mq_read()` 以 `kfifo_out(&g_fifo, kb, MSG_SIZE)` 減少資料。
- synchronization：
  - `g_lock` 保護 actual in/out operation。
  - `g_rd_wq` / `g_wr_wq` 提供 blocking wait。

#### Character Device Resource：`g_devno` / `g_cdev` / `g_class`

# Direct Observation

- MQ 與 SHM module 各自有自己的 `g_devno`、`g_cdev`、`g_class`。
- allocation / init：
  - `alloc_chrdev_region()` -> `cdev_init()` -> `cdev_add()` -> `class_create()` -> `device_create()`。
- release timing：
  - `device_destroy()` -> `class_destroy()` -> `cdev_del()` -> `unregister_chrdev_region()`。
- ownership：
  - module owns registration；user only owns opened fd。

#### `targ_t`

# Direct Observation

- 定義位置：`user/benchmark.c`。
- 欄位：
  - `int fd`
  - `shm_region_t *shm`
  - `int count`
  - `int is_producer`
  - `pthread_barrier_t *bar`
  - `double elapsed_us`
- allocation / init：
  - `run_test()` 內以 stack local `pa` / `ca` 建立 producer/consumer args。
- ownership / lifetime：
  - `run_test()` owns stack object。
  - worker thread 在 `pthread_join()` 前使用該指標。
- data passing path：
  - `pthread_create()` 把 `&pa`、`&ca` 傳給 worker。

#### Proc Stats Resource

# Direct Observation

- `proc_create()` return value 目前未存到 global pointer。
- cleanup 使用 `remove_proc_entry("mq_stats", NULL)` 與 `remove_proc_entry("shm_stats", NULL)` 依名稱移除。
- stats data：
  - MQ：`st_enq`、`st_deq`、`st_lat_ns_total`、`st_last_enq_ts`、`kfifo_len()`、`kfifo_avail()`。
  - SHM：`st_wr`、`st_rd`、`st_lat_ns_total`、`st_last_wr_ts`、`g_shm->head/tail/capacity/msg_size`。

---

### 6. Execution Trace（文字流程圖）

#### Initialization Flow

# Direct Observation

```text
[setup script]
01_setup.sh
  -> root check
  -> install build dependencies
  -> build kernel modules
  -> build user binaries
  -> insmod mq_module.ko
  -> insmod shm_module.ko
  -> chmod /dev/mq_ipc /dev/shm_ipc
  -> read /proc/mq_stats and /proc/shm_stats line counts

[mq module]
mq_init
  -> initialize counters
  -> register char device
  -> bind g_fops
  -> create /dev/mq_ipc
  -> create /proc/mq_stats

[shm module]
shm_init
  -> vmalloc shared ring
  -> initialize capacity/msg_size/head/tail
  -> initialize spinlock and counters
  -> register char device
  -> bind g_fops
  -> create /dev/shm_ipc
  -> create /proc/shm_stats
```

#### Runtime Flow

# Direct Observation

```text
[benchmark main]
main
  -> parse count
  -> open /dev/mq_ipc
  -> open /dev/shm_ipc
  -> mmap /dev/shm_ipc
  -> run_test("Message Queue", mqfd, NULL, count, false)
  -> run_test("Shared Memory (syscall)", shmfd, NULL, count, false)
  -> reset shm->head = shm->tail = 0
  -> run_test("Shared Memory (mmap)", shmfd, shm, count, true)
  -> system("cat /proc/mq_stats")
  -> system("cat /proc/shm_stats")
  -> munmap/close
```

#### Cleanup Flow

# Direct Observation

```text
[user process cleanup]
benchmark/shm_demo
  -> munmap(shm, SHM_MAP_SIZE)
  -> close(fd)
  -> kernel .release callback returns 0

[module cleanup]
04_cleanup.sh or manual rmmod
  -> rmmod shm_module
       -> shm_exit
       -> remove /proc/shm_stats
       -> remove /dev/shm_ipc registration
       -> vfree(g_shm)
  -> rmmod mq_module
       -> mq_exit
       -> remove /proc/mq_stats
       -> remove /dev/mq_ipc registration
```

#### Data Flow

# Direct Observation

```text
MQ:
producer user buf
  -> copy_from_user()
  -> stack kb[64]
  -> kfifo
  -> stack kb[64]
  -> copy_to_user()
  -> consumer user buf

SHM syscall:
producer user buf
  -> copy_from_user()
  -> g_shm->data[head]
  -> memcpy(tmp, g_shm->data[tail])
  -> copy_to_user()
  -> consumer user buf

SHM mmap:
producer user fill[64]
  -> memcpy(shm->data[head], fill, 64)
  -> shared mapped pages
  -> memcpy(sink, shm->data[tail], 64)
  -> consumer stack sink[64]
```

#### Event Flow

# Direct Observation

```text
MQ write event:
  FIFO has >= 64 bytes free
  -> writer enqueues
  -> wake_up_interruptible(g_rd_wq)

MQ read event:
  FIFO has >= 64 bytes data
  -> reader dequeues
  -> wake_up_interruptible(g_wr_wq)

SHM syscall event:
  write sees ring not full
  -> updates head
  read sees ring not empty
  -> updates tail

SHM mmap event:
  producer/consumer spin on head/tail visibility
  -> no kernel wakeup per message
```

#### Ownership Transfer

# Direct Observation

```text
MQ:
user owns original buffer
kernel owns copied FIFO bytes
consumer receives a copy
no pointer ownership crosses boundary

SHM syscall:
kernel owns g_shm allocation
user writes/reads through syscall copies
no mapping ownership transfer

SHM mmap:
kernel owns backing g_shm allocation
user owns VMA mapping until munmap/process exit
producer/consumer coordinate slots by head/tail, not by object ownership token
```

---

## 第二階段：Architecture / API Technical Report

### 1. Entry Point 行為

# Direct Observation

#### Kernel entry points

- `mq_init()` 是 MQ module load entry。
  - 建立 char device 與 proc stats。
  - 沒有配置 per-open state。
  - `.open` / `.release` callback 只回傳 0。

- `shm_init()` 是 SHM module load entry。
  - 先配置並初始化 `g_shm`，再註冊 char device。
  - mmap path 的 backing memory 在 module lifetime 內維持同一份 global region。
  - `.open` / `.release` callback 只回傳 0。

#### User entry points

- `user/benchmark.c:main()`
  - 解析 count。
  - 開啟兩個 device。
  - 對 SHM device 做一次 `mmap()`。
  - 依序測三種 path。
  - 最後讀取 `/proc/mq_stats` 與 `/proc/shm_stats`。

- `user/mq_demo.c:main()`
  - 開啟 `/dev/mq_ipc`。
  - 連續寫入 `DEMO_N` 筆，再讀出 `DEMO_N` 筆。
  - 最後 `cat /proc/mq_stats`。

- `user/shm_demo.c:main()`
  - 開啟 `/dev/shm_ipc`。
  - `mmap()` shared region。
  - 重設 `head/tail`。
  - 用 user-space producer/consumer loop 操作 ring。
  - 最後 `cat /proc/shm_stats`。

# Conservative Inference

因為 `.open` 沒有配置 private data，且 `g_fifo` / `g_shm` 皆為 module-global resource，目前行為是所有 opener 共用同一份 queue/ring。程式碼中未觀察到 per-client isolation。

---

### 2. Callback Registration Chain

# Direct Observation

```text
MQ:
mq_init()
  -> cdev_init(&g_cdev, &g_fops)
       g_fops.write = mq_write
       g_fops.read = mq_read
       g_fops.open = mq_open
       g_fops.release = mq_release
  -> proc_create("mq_stats", ..., &g_proc_ops)
       g_proc_ops.proc_open = mq_proc_open
       mq_proc_open -> single_open(..., mq_stats_show, NULL)

SHM:
shm_init()
  -> cdev_init(&g_cdev, &g_fops)
       g_fops.write = shm_write
       g_fops.read = shm_read
       g_fops.mmap = shm_mmap
       g_fops.open = shm_open
       g_fops.release = shm_release
  -> proc_create("shm_stats", ..., &g_proc_ops)
       g_proc_ops.proc_open = shm_proc_open
       shm_proc_open -> single_open(..., shm_stats_show, NULL)
```

目前程式碼中未觀察到 callback deregistration pointer 保存；cleanup 以 `remove_proc_entry(name, NULL)` 依名稱移除 proc entry。

---

### 3. Runtime Dispatch Flow 與 Indirect Call Path

# Direct Observation

#### MQ

- `write()` 不會直接呼叫 `mq_write()`；它經過 VFS 與 `g_fops.write` function pointer dispatch。
- `read()` 不會直接呼叫 `mq_read()`；它經過 VFS 與 `g_fops.read` function pointer dispatch。
- `cat /proc/mq_stats` 經過 procfs、`g_proc_ops.proc_open`、`single_open()`，最後由 seq_file 呼叫 `mq_stats_show()`。

#### SHM

- `write()` / `read()` 經過 `shm_module.c:g_fops` dispatch 到 `shm_write()` / `shm_read()`。
- `mmap()` 經過 `g_fops.mmap` dispatch 到 `shm_mmap()`。
- mmap 建立後，每筆 message 的 producer/consumer 操作不再經過 kernel callback，而是直接操作 mapped `shm_region_t`。

#### Benchmark

- `run_test()` 不是直接呼叫 worker function 執行 loop，而是透過 `pthread_create()` 的 start routine function pointer 啟動。
- `use_mmap == true` 時傳 `mmap_worker`；否則傳 `syscall_worker`。

# Conservative Inference

SHM mmap path 的效能差異主要來自每筆 message 避開 VFS syscall dispatch 與 `copy_from_user()` / `copy_to_user()`，這是由 `mmap_worker()` 直接 `memcpy()` mapped region 可直接驗證出的保守推論。

---

### 4. Resource Lifecycle / Ownership Transition

# Direct Observation

#### MQ resource model

- `g_fifo` 是 static module-global FIFO。
- writer 只把資料 copy 進 FIFO，不移交 user pointer。
- reader 只從 FIFO copy 出資料，不取得 FIFO slot ownership。
- flow 中沒有 per-message allocation/free。
- module exit 沒有特別 drain FIFO；module unload 會移除整個 device 與 module storage。

#### SHM syscall resource model

- `g_shm` 由 kernel `vmalloc()` 建立。
- `shm_write()` 用 `head` 選 slot，成功後更新 `head`。
- `shm_read()` 用 `tail` 選 slot，成功後更新 `tail`。
- ring full/empty 以 return code 表達，不使用 wait queue。
- `g_shm` 在 `shm_exit()` 統一 `vfree()`。

#### SHM mmap resource model

- kernel 仍 owns `g_shm` backing pages。
- user process obtains shared mapping through `mmap()`。
- user producer/consumer 直接修改 `head/tail/data`。
- `munmap()` 只解除 user VMA，不釋放 kernel backing memory。
- module unload 釋放 `g_shm`；目前程式碼中未觀察到 custom `vm_operations_struct` 追蹤 mapping open/close。

# Conservative Inference

因為沒有 `vm_ops` reference tracking，若 module unload 時仍有 user mapping，實際安全性會依 kernel module refcount / VMA 對 file 的持有行為而定；目前 code 中無法從 module 自身確認它有額外保護 active mapping lifetime 的機制。

---

### 5. Error Propagation Path

# Direct Observation

#### Kernel -> User

- MQ：
  - `copy_from_user()` failure -> `mq_write()` 回 `-EFAULT`。
  - writer wait 被 signal interrupt -> `-EINTR`。
  - `mq_read()` length < 64 -> `-EINVAL`。
  - `O_NONBLOCK` 且 FIFO 不足 -> `-EAGAIN`。
  - `copy_to_user()` failure -> `-EFAULT`。

- SHM：
  - `vmalloc()` failure -> `shm_init()` 回 `-ENOMEM`。
  - `shm_write()` ring full -> `-ENOSPC`。
  - `shm_read()` ring empty -> `-EAGAIN`。
  - read length < 64 -> `-EINVAL`。
  - `shm_mmap()` requested size > `SHM_BUF_SIZE` -> `-EINVAL`。
  - `remap_pfn_range()` failure -> 立即回該 error。

#### User handling

- `benchmark.c`
  - `open()` / `mmap()` failure 會 `perror()`。
  - `syscall_worker()` 對 `EAGAIN`、`ENOSPC`、`EINTR` retry。
  - `pthread_create()`、`pthread_join()`、`pthread_barrier_init()` return value 目前未檢查。
  - `mmap()` failure 走 `goto done`，最後仍 `return 0`。

- `mq_demo.c`
  - `open()` failure 會 `perror()` 並 `return 1`。
  - `write()` / `read()` return value 目前未檢查。

- `shm_demo.c`
  - `open()` / `mmap()` failure 會回 `1`。
  - mmap ring full/empty 使用 busy-spin，沒有 timeout。

---

### 6. 類似 API / Mechanism 比較分析

#### MQ vs SHM syscall vs SHM mmap

# Direct Observation

| 比較項 | MQ syscall | SHM syscall | SHM mmap |
| --- | --- | --- | --- |
| external interface | `/dev/mq_ipc` read/write | `/dev/shm_ipc` read/write | `/dev/shm_ipc` mmap + user pointer access |
| storage | `DEFINE_KFIFO(g_fifo)` | `vmalloc()` ring `g_shm` | 同一份 `g_shm` 映射到 user |
| dispatch | VFS `.read/.write` | VFS `.read/.write` | 只有 setup 經 VFS `.mmap`；每筆 message 不經 VFS |
| copy count | user -> kernel stack -> kfifo；kfifo -> kernel stack -> user | user -> shared ring；shared ring -> kernel stack -> user | user memcpy -> shared ring；shared ring -> user memcpy |
| full behavior | writer 在 wait queue sleep | `shm_write()` 回 `-ENOSPC` | user producer busy-spin |
| empty behavior | reader 可 sleep 或 nonblock `-EAGAIN` | `shm_read()` 回 `-EAGAIN` | user consumer busy-spin |
| synchronization | `mutex` + wait queues | `spinlock` + memory barriers | volatile head/tail + `__sync_synchronize()` |
| stats | `/proc/mq_stats` | `/proc/shm_stats` | 使用同一份 SHM stats，但 mmap per-message 不會更新 `st_wr/st_rd` |

#### `mq_write()` vs `shm_write()`

# Direct Observation

- 相同點：
  - 都接受 user buffer。
  - 都把 `len > MSG_SIZE` clamp 到 64。
  - 都成功回傳 `MSG_SIZE`。
  - 都更新 atomic counter 與 global last timestamp。

- 差異：
  - `mq_write()` 在 `copy_from_user()` 後，如果 FIFO 沒空間會 sleep 等待；`shm_write()` 不等待，ring full 直接回 `-ENOSPC`。
  - `mq_write()` 用 `mutex` 保護 `kfifo_in()`；`shm_write()` 用 `spin_lock` 保護 ring state。
  - `mq_write()` 寫入 `kfifo`；`shm_write()` 寫入 `g_shm->data[head]` 並更新 `head`。

#### `mq_read()` vs `shm_read()`

# Direct Observation

- 相同點：
  - `len < MSG_SIZE` 都回 `-EINVAL`。
  - 成功時都複製 64 bytes 給 user。
  - 都更新 read/dequeue counter 與 latency total。

- 差異：
  - `mq_read()` 可以 blocking wait；`shm_read()` ring empty 直接回 `-EAGAIN`。
  - `mq_read()` 讀完會 `wake_up_interruptible(&g_wr_wq)`；`shm_read()` 沒有 wait queue。
  - `mq_read()` 從 `kfifo` 取資料；`shm_read()` 從 `g_shm->data[tail]` 取資料並更新 `tail`。

#### Callback 機制差異

# Direct Observation

- MQ 與 SHM syscall path 都透過 `struct file_operations`。
- SHM 額外提供 `.mmap = shm_mmap`；MQ 沒有 mmap callback。
- Proc stats 兩者都透過 `struct proc_ops` + `single_open()` + `seq_read()`。
- User benchmark 的 callback 是 pthread start routine，不是 kernel callback。

#### Dispatch Model 使用原因（基於 code）

# Direct Observation

- `cdev_init(&g_cdev, &g_fops)` 是兩個 module 讓 `/dev/*` 對應 read/write/mmap callback 的直接原因。
- `proc_create(..., &g_proc_ops)` 是 `/proc/*_stats` 讀取 stats callback 的直接原因。
- `pthread_create(..., use_mmap ? mmap_worker : syscall_worker, ...)` 是 benchmark 在 runtime 選擇 execution model 的直接原因。

# Conservative Inference

程式以相同 `MSG_SIZE` 與類似 producer/consumer loop 比較三種 dispatch model：kernel blocking queue、kernel ring syscall、user-space mapped ring。這是由 `benchmark.c` 的三次 `run_test()` 呼叫順序與參數推得。

---

### 7. Debug / Risk Analysis

以下只列目前 code 中能觀察或保守推論出的風險。

#### Potential Memory Leak / Resource Leak

# Direct Observation

- `shm_init()` 的 init failure path 有 `vfree(g_shm)`，正常 `shm_exit()` 也有 `vfree(g_shm)`。
- `mq_init()` 沒有 dynamic heap allocation。
- `proc_create()` 的 return value 在 `mq_init()` 與 `shm_init()` 都沒有檢查，也沒有保存 pointer。

# Conservative Inference

- `proc_create()` 失敗時 module 仍可能完成載入，但 `/proc/*_stats` 不存在；這不是直接 memory leak，但會造成 registration state 與 script/reporting expectation 不一致。
- `shm_mmap()` 在 page loop 中若 `remap_pfn_range()` 中途失敗，function 直接回傳 error；目前程式碼中未觀察到 module 自行 rollback 已映射頁面的邏輯。實際 VMA cleanup 由 kernel mmap error path 處理，無法只從本 module 確認完整行為。

#### Invalid Ownership Transfer / ABI Risk

# Direct Observation

- mmap path 讓 user 直接寫 `shm->head`、`shm->tail` 與 `shm->data`。
- `user/common.h:shm_region_t` 與 `kernel/shm_module.c:struct shm_region` 手動維持相同 layout。

# Conservative Inference

- 目前未觀察到 layout version、magic number、static assert 或 runtime validation；若 user/header 與 kernel module 不同步，user mmap path 可能錯誤解讀 ring metadata。
- mmap path 沒有 per-slot ownership token；producer/consumer 僅靠 head/tail 協定。若多 producer 或多 consumer 同時使用，目前程式碼中未觀察到可保證正確性的同步機制。

#### Callback Misuse Risk

# Direct Observation

- `.open` / `.release` 只回 0，沒有 per-open state 或 active mapping tracking。
- `proc_create()` return value 未檢查。
- `benchmark.c` 的 `pthread_create()` / `pthread_join()` return value 未檢查。

# Conservative Inference

- 若 pthread 建立失敗，`run_test()` 仍可能進行 join 或使用未完成的 timing data；目前 user code 沒有 error propagation。
- 若 proc entry 建立失敗，`system("cat /proc/*_stats")` 會依 shell command 結果失敗，但 benchmark 本身沒有檢查 `system()` return code。

#### Lifecycle Mismatch

# Direct Observation

- `scripts/01_setup.sh` 執行 `make -C "${PROJECT_DIR}" kernel`。
- `benchmark.c` 的 `mmap()` 失敗會 `goto done`，最後 `return 0`。
- `scripts/04_cleanup.sh` 依序卸載 `shm_module` 再卸載 `mq_module`，與兩個 module 間目前沒有直接依賴關係。

# Conservative Inference

- `benchmark.c` 在 mmap failure 下回傳 0 可能讓 shell script 誤判 benchmark 成功。

#### Concurrency Issue

# Direct Observation

- `shm_write()` 在 `spin_lock(&g_spin)` 後呼叫 `copy_from_user()`。
- `copy_from_user()` 可能 fault；這段目前位於 spinlock critical section。
- SHM mmap path 的 producer/consumer busy-spin 沒有 `sched_yield()`、`pause` 或 blocking primitive。
- `mq_stats_show()` 直接讀 `kfifo_len()` / `kfifo_avail()`；`shm_stats_show()` 直接讀 `g_shm->head/tail`，未使用對應 lock 包住整個 snapshot。

# Conservative Inference

- 在 kernel 中於 spinlock 內呼叫可能 sleep/fault 的 user copy API 是高風險設計；若發生 page fault 或 sleep 需求，可能違反 spinlock context 的限制。
- mmap busy-spin 在 ring full/empty 時會消耗 CPU；目前程式碼中未觀察到 timeout 或 backoff。
- proc stats 讀取可能得到瞬間不一致 snapshot；這影響 debug 數字一致性，不一定影響 data path 正確性。

#### Data Correctness / Partial Message Risk

# Direct Observation

- `mq_write()` 若 caller `len < MSG_SIZE`，只 `copy_from_user(kb, ubuf, len)`，但仍 `kfifo_in(..., MSG_SIZE)` 並回 `MSG_SIZE`。
- `shm_write()` 若 caller `len < MSG_SIZE`，只 copy `len` bytes 到 slot，但仍更新 `head` 並回 `MSG_SIZE`。
- `mq_demo.c` 與 `benchmark.c` 實際都以 `MSG_SIZE` 呼叫 write；但 API 本身沒有拒絕 short write。

# Conservative Inference

- 若外部 caller 使用小於 64 bytes 的 write，MQ 的 stack buffer 未填滿部分可能被寫入 FIFO；SHM slot 未覆寫部分可能保留舊資料。這是基於目前 copy 長度與固定 slot commit 行為的保守推論。

#### Latency Statistic Risk

# Direct Observation

- MQ 使用單一 global `st_last_enq_ts`，每次 `mq_write()` 更新；`mq_read()` 以目前讀取時間減去這個 global timestamp。
- SHM 使用單一 global `st_last_wr_ts`，每次 `shm_write()` 更新；`shm_read()` 以目前讀取時間減去這個 global timestamp。

# Conservative Inference

- 當 queue/ring 中累積多筆 message 時，read latency 不一定對應該筆 message 的 enqueue/write timestamp；因此 `/proc/*_stats` 的 average latency 比較接近「最後一次 write 到 read 的差值累積」，不是 per-message latency。
- mmap path 每筆 message 不呼叫 kernel `shm_write()` / `shm_read()`，所以 `/proc/shm_stats` 的 `writes/reads` 不會反映 mmap worker 的每筆 user-space head/tail 操作。

---

### 8. Conservative Inference Summary

- 目前程式碼展示的是單一 module-global queue/ring，而非 per-client IPC channel。
- mmap path 的主要成本差異來自避開每筆 syscall dispatch 與 user/kernel copy。
- SHM mmap ring 依賴 single producer / single consumer 形式的 head/tail 協定；目前程式碼中未觀察到 multi-producer / multi-consumer 的正確性保護。
- `/proc/*_stats` 可用於觀察 module counters，但 latency 與 snapshot 一致性都有目前實作上的限制。

---

## 結論

# Direct Observation

`linux-ipc-benchmark` 目前實作了兩個 kernel char device module 與三條可比較的 IPC runtime path：

1. `/dev/mq_ipc`：`kfifo` + `mutex` + `wait_queue` 的 blocking Message Queue。
2. `/dev/shm_ipc` syscall：`vmalloc` ring + `spinlock` + `copy_from_user/copy_to_user`。
3. `/dev/shm_ipc` mmap：`vmalloc` backing pages 經 `remap_pfn_range()` 映射到 user，由 user-space thread 直接操作 `head/tail/data`。

主要 callback chain 由 `struct file_operations`、`struct proc_ops` 與 `pthread_create()` start routine 組成；主要 ownership 差異在於 MQ/SHM syscall path 都不移交 pointer ownership，而 mmap path 讓 user 取得 shared VMA 並直接參與 ring state transition。

# Conservative Inference

目前 code 的核心價值是以可執行的 minimal module 對比 IPC dispatch、copy count、blocking/spinning 與 memory sharing 成本；但若要作為更一般化 IPC framework，還需要補強 short write handling、spinlock 中 user copy、mmap lifetime tracking、thread/system call error propagation，以及 multi-client concurrency 語意。
