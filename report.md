# Linux Kernel & Firmware Engineering Portfolio — 技術報告

本報告依各子專題**現有原始碼**整理：模組分工、初始化與執行時的函式呼叫順序、資料流、關鍵 API 行為，以及開發與除錯時實際會踩到的限制。子目錄另有 `report_*.md` 可對照細節；本頁著重**跨模組全景**與**可從程式碼直接驗證**的敘述。

---

## 一、技術與工具總覽

| 類別 | 本儲存庫實際使用 |
|------|------------------|
| 語言 | C11（`-std=c11`，如 `ssd-fw-sim/Makefile`、`fwsh/Makefile`） |
| 建置 | GNU Make；kernel 模組用 `KDIR=/lib/modules/$(uname -r)/build` |
| 使用者介面 | VFS（read/write/ioctl）、sysfs、procfs、mmap |
| 同步 | `mutex`、`spinlock`、`wait_queue`、`atomic_t` / `atomic64_t` |
| 記憶體 | `kzalloc`/`kfree`、`vmalloc`/`vfree`、`calloc`/`free`、`devm_*` |
| 模擬/腳本 | QEMU、cloud-init（`cpu-scheduling-qemu`）、Device Tree overlay |
| 測試 | `ssd-fw_sim_tests`（`assert` 回歸）、各子專題 shell 腳本 |

**目前程式碼中未觀察到**：真實硬體 ISR、DMA engine、FreeRTOS、kernel scheduler patch。

---

## 二、整體架構與模組關係

六個子專題在**原始碼層互不連結**（無跨目錄 `#include`），共同形成能力矩陣：

```text
                    ┌─────────────────────────────────────┐
                    │  User space                         │
                    │  ssd-fw-sim  fwsh  cpu-scheduling   │
                    │  ipc/benchmark  chardev/test_app    │
                    └──────────────┬──────────────────────┘
                                   │ syscall / VFS
                    ┌──────────────▼──────────────────────┐
                    │  Kernel modules (可選載入)            │
                    │  chardev  mq_module  shm_module     │
                    │  myled_ctrl (QEMU ARM64 initramfs)  │
                    └─────────────────────────────────────┘
```

| 模組 | 核心檔案 | 主要抽象 |
|------|----------|----------|
| ssd-fw-sim | `main.c`, `nvme.c`, `scheduler.c`, `ftl.c`, `gc.c`, `nand.c` | NVMe 環形佇列 + FTL + 虛擬時間 |
| chardev-driver | `driver/chardev.c` | `file_operations` + 全域 `drv` 狀態 |
| linux-ipc-benchmark | `mq_module.c`, `shm_module.c`, `user/benchmark.c` | kfifo vs vmalloc ring + mmap |
| fwsh | `shell.c`, `parser.c`, `executor.c`, `builtin.c` | REPL + fork/pipe |
| qemu-platform-demo | `driver/myled_ctrl.c` | `platform_driver` + OF + MMIO/sysfs |
| cpu-scheduling-qemu | `src/scheduler.c` | 離散時間排程模擬（stdin workload） |

---

## 三、模組一：`ssd-fw-sim`

### 3.1 角色與邊界

User-space **單執行緒**模擬器：讀取 trace 檔中的 `WRITE` 指令，驅動簡化 NVMe SQ/CQ、內部 `request_queue_t`、FTL 寫入與 greedy GC，最後輸出 `ssd_statistics_t`。

全域狀態：`ftl_context_t g_ftl`（`ftl.c`），由 `ftl_init`/`ftl_destroy` 管理生命週期。

### 3.2 初始化順序（實際 `main.c`）

```text
main()
├── ssd_config_init_default(&config)
├── [可選] ssd_config_load_file + ssd_config_validate
├── request_queue_init(&request_queue, depth)
├── nvme_controller_init(&nvme, depth)     // calloc SQ/CQ entries
├── ftl_init(&config)
│   ├── nand_init
│   ├── calloc mapping_table, lpn_write_count
│   ├── free_block_pool_init + 所有 block push + pop 一個作為 current_write_block
│   └── stats_init
├── replay_trace(...) 或錯誤處理
├── while (nvme_has_pending) service_nvme_pipeline
├── stats_print / stats_export_csv
└── ftl_destroy → nvme_controller_destroy → request_queue_destroy
```

### 3.3 執行時資料流（單筆 WRITE）

```text
replay_trace
  → nvme_submit_write(slba, nlb, submit_timestamp_us)   // SQ tail 入隊
  → service_nvme_pipeline
       → nvme_issue_pending → request_queue_enqueue        // SQ → RQ
       → scheduler_run
            → [gc_needed] gc_run(false)
            → ftl_handle_request → ftl_handle_write
                 → 每 LPN: [gc] allocate → program → invalidate old → mapping_set
            → nvme_post_completion(SUCCESS, current_time_us)
            → stats_update_request(queue/service/total latency)
       → nvme_reap_completions
```

**背壓**：`nvme_submit_write` 回 `-1` 時（SQ 滿），先 `service_nvme_pipeline` 再重試（`main.c`）。

### 3.4 關鍵函式對照

| 函式 | 回傳/行為 | 與誰搭配 |
|------|-----------|----------|
| `nvme_submit_write` | 0 / -1（SQ full） | 僅寫入 SQ，不觸發 FTL |
| `nvme_issue_pending` | 發行筆數 | 需 `request_queue` 未滿 |
| `scheduler_run` | false 若 FTL 失敗或 CQ 滿 | 更新 `g_ftl.current_time_us`、延遲欄位 |
| `ftl_handle_write` | bool | 僅 `REQUEST_TYPE_WRITE` |
| `gc_run(ftl, foreground)` | 選 victim → migrate → erase → push free pool | `gc_needed` 看 `free_block_pool_count` vs threshold |
| `nand_allocate_page` | 失敗時上層觸發 `gc_run(true)` | 依 `write_pointer` 換 block |

**橫向比較**：

- `gc_run(false)`（background）在 `scheduler_run` 與 `ftl_handle_write` 內都可能呼叫；`gc_run(true)` 僅在 allocate 失敗路徑（foreground）。
- `free_block_pool_get_min_erase_block`（`block_manager.c`）**已實作但 GC 未呼叫**；victim 使用 `gc_select_victim_block` 的 max-invalid 策略。

### 3.5 狀態與統計

- NAND page：`FREE → VALID`（program）→ `INVALID`（invalidate）→ block erase 後再 `FREE`。
- `stats_write_amplification` = `nand_write_count / host_page_count`（`stats.c`）。
- `tests/test_suite.c` 驗證：`total_queue_latency_us + total_service_latency_us == total_latency_us`。

### 3.6 已知限制（程式碼可證）

- 不支援 READ；trace 非 `WRITE` 行被略過。
- 無多執行緒鎖；`g_ftl` 不可重入。
- CQ phase bit 有設定（`nvme_post_completion`），reap 時未驗證 phase。
- 無斷電恢復、journal（映射表僅在 RAM）。

更細 API 清單見 [`ssd-fw-sim/report_ssd_api.md`](ssd-fw-sim/report_ssd_api.md)。

---

## 四、模組二：`chardev-driver`

### 4.1 初始化（`chardev_init`）

```text
kzalloc(BUF_SIZE) → mutex_init → alloc_chrdev_region
→ cdev_init/add → class_create(CLASS_NAME) → dev_groups
→ device_create("chardev0") → proc_create("chardev_info")
```

失敗時 `err_device` → `err_class` → `err_cdev` → `err_region` → `err_buf`。

### 4.2 執行路徑

| 操作 | 函式 | 同步 | 使用者資料 |
|------|------|------|------------|
| read | `chardev_read` | `mutex` | `copy_to_user`，尊重 `*ppos` |
| write | `chardev_write` | `read_only` 檢查在 mutex **外** | `copy_from_user`，`buf_len` 設為寫入長度 |
| ioctl | `chardev_ioctl` | RESET 有 mutex；SET_RDONLY 無 | magic `'k'`，`-ENOTTY`/`-EFAULT` |

### 4.3 橫向比較：觀測介面

| 介面 | 用途 |
|------|------|
| read/write | 資料面 4KB buffer |
| ioctl | `IOCTL_RESET_BUF`、`IOCTL_GET_LEN`、`IOCTL_SET_RDONLY` |
| proc | 一次 dump 狀態與 buffer 內容 |
| sysfs | `buf_len`（RO）、`read_only`（RW）、`stats`（RO） |

### 4.4 除錯與風險

- **Race**：`drv.read_only` 可由 ioctl/sysfs 與 `chardev_write` 並行修改（write 在鎖外檢查）。
- **語意**：write 覆寫 buffer，非 append；與一般 seekable 裝置行為需在文件說明。
- **驗證**：`userspace/test_app.c` 覆蓋 open/write/read/ioctl；`load.sh` 用 `dmesg` 確認載入。

---

## 五、模組三：`linux-ipc-benchmark`

### 5.1 兩條 kernel 路徑

**MQ（`mq_module.c`）**

```text
mq_write: wait_event(wr_wq, kfifo_avail >= MSG_SIZE)
         → mutex → kfifo_in → wake rd_wq
mq_read:  wait_event 或 O_NONBLOCK
         → mutex → kfifo_out → copy_to_user → wake wr_wq
```

固定訊息大小 `MSG_SIZE=64`；`FIFO_SIZE = 64 * 512`。

**SHM syscall（`shm_module.c`）**

```text
shm_write: spin_lock → full? -ENOSPC → copy_from_user(slot) → smp_wmb → head++
shm_read:  spin_lock → empty? -EAGAIN → smp_rmb → memcpy tmp → tail++ → copy_to_user
```

**SHM mmap**

```text
shm_mmap: 逐頁 vmalloc_to_pfn + remap_pfn_range
user benchmark mmap_worker: 直接寫 shm->data[head]，__sync_synchronize，無 per-message syscall
```

### 5.2 使用者 `benchmark.c` 三項測試

| 測試 | 機制 | 目的 |
|------|------|------|
| [1] | `/dev/mq_ipc` write/read | kfifo + 阻塞 waitqueue |
| [2] | `/dev/shm_ipc` write/read | 隔離「ring 實作」與 MQ 差異，仍 2-copy |
| [3] | mmap + 指標操作 | 量測去掉 syscall/copy 後的吞吐 |

`pthread_barrier` 同步雙執行緒開跑；Test 3 前將 `shm->head = tail = 0`。

### 5.3 橫向比較：同步原語

| 模組 | 滿時行為 | 鎖 |
|------|----------|-----|
| mq | 睡眠（可 `-EINTR`） | mutex + waitqueue |
| shm syscall | 立即 `-ENOSPC` / `-EAGAIN` | spinlock |
| shm mmap（user） | busy-spin | **kernel 無鎖**（假設 SPSC） |

### 5.4 限制

- `struct shm_region` 須與 `user/common.h` 的 `shm_region_t` layout 一致。
- mmap 路徑多 producer 會 race（程式未防護）。
- `benchmark.c` 結尾用 `system("cat /proc/...")` 僅方便 demo，非嚴謹量測方法。

---

## 六、模組四：`fwsh`

### 6.1 生命週期

```text
main → shell_init (SIGCHLD/SIGINT/SIGTSTP, readline)
     → shell_run loop
          readline → parse_line → execute_pipeline → free_pipeline
     → shell_cleanup
```

### 6.2 `execute_pipeline` 決策

1. **單指令、前景、builtin** → 不 fork，`exec_builtin`（例如 `cd` 改當前 process cwd）。
2. **否則** → `pipe` × (ncmds-1)，每段 `fork`，子行程 `dup2` 接線後 `execvp` 或 builtin 後 `_exit`。
3. **父行程**關閉所有 pipe fd（否則讀端永不 EOF）。
4. **前景** `waitpid`；**背景** 印 PID，靠 `SIGCHLD` + `waitpid(-1, WNOHANG)` 回收。

### 6.3 Builtin 分派

`builtin.c` 的 `builtins[]` 函式表：`hexdump`、`crc32`（IEEE 802.3）、`memmap`（讀 `/proc/iomem`）。

### 6.4 限制

- 無 job control（`fg`/`bg`/`jobs`）。
- 引號支援子集，無 `$VAR` 展開。
- pipeline 中 builtin 在子 process 執行。

---

## 七、模組五：`qemu-platform-demo`

### 7.1 Probe 流程（`myled_probe`）

```text
devm_kzalloc(priv) → of_property_read num-leds/label
→ platform_get_resource(MEM) → devm_ioremap_resource 或 simulated=true
→ myled_hw_init (讀 VERSION，不符則 fallback sim_regs)
→ sysfs_create_group("myled")
→ pm_runtime_enable
```

### 7.2 暫存器存取

`myled_reg_read/write`：`spin_lock_irqsave` 後選 `readl/writel` 或 `sim_regs[off/4]`。

sysfs：`enable`、`brightness`、`color`、`blink`、`status`、`info`。

### 7.3 限制

- **目前無 IRQ handler**；PM 僅 suspend/resume 清/設 ENABLE bit。
- MMIO 讀到非 `MYLED_HW_VERSION`（含 `0xffffffff`）時切 simulated（`myled_hw_init` 註解）。

---

## 八、模組六：`cpu-scheduling-qemu`

### 8.1 實際行為

`scheduler.c` 從 **stdin** 讀 `n` 與 `pid arrival burst priority`，依 argv 呼叫 `sched_fcfs` / `sched_sjf` / `sched_srtf` / `sched_priority` / `sched_rr`。

輸出 Gantt（`gantt_push` 合併相鄰同 PID）與 `BENCHMARK ... AWT= ATT= ART=` 行供腳本解析。

### 8.2 與 QEMU 的關係

`scripts/01_setup_env.sh` 在主機編譯 `scheduler`，透過 cloud-init 放入 VM；**並未修改 guest kernel 排程程式碼**。

### 8.3 限制

- 離散時間模型（SRTF 每 tick 一步），非真實 CFS vruntime。
- RR 的 ready queue 邏輯較複雜，邊界需對照 `sched_rr` 原始碼驗證。

---

## 九、關鍵函式橫向比較（跨專題）

本章把六個子專題裡**性質相近的函式**放在一起比：它們解決的問題類似，但實作位置（user/kernel）、阻塞語意、同步原語與錯誤回傳不同。下列內容皆可對照各目錄 `*.c` 驗證。

### 9.1 依「工程需求」選模組（速查）

| 你想教/證明什麼 | 優先看 | 關鍵符號 |
|-----------------|--------|----------|
| 完整 char 驅動 + 多種 user 介面 | `chardev-driver` | `chardev_fops`、`chardev_init` |
| user/kernel copy 次數與 mmap | `linux-ipc-benchmark` | `mq_write`、`shm_write`、`shm_mmap`、`mmap_worker` |
| Device Tree + MMIO + sysfs | `qemu-platform-demo` | `myled_probe`、`myled_reg_read` |
| FTL / GC / 儲存延遲模型 | `ssd-fw-sim` | `ftl_handle_write`、`gc_run`、`service_nvme_pipeline` |
| fork / pipe / 信號 | `fwsh` | `execute_pipeline`、`sigchld_handler` |
| 排程演算法數字與 Gantt | `cpu-scheduling-qemu` | `sched_srtf`、`sched_rr`、`gantt_push` |

---

### 9.2 初始化與資源釋放（誰分配、誰釋放）

| 模組 | 入口 | 主要分配 | 失敗回滾 | 清理入口 |
|------|------|----------|----------|----------|
| **chardev** | `chardev_init` | `kzalloc` buffer、`alloc_chrdev_region`、`cdev_add`、`class_create`、`device_create`、`proc_create` | `goto err_*` 反向 | `chardev_exit`：`proc_remove` → `device_destroy` → … → `kfree` |
| **mq_module** | `mq_init` | `DEFINE_KFIFO`（靜態）、`alloc_chrdev_region`、`cdev_add` | `err_dev` → `err_class` → `err_cdev` | `mq_exit`：proc、device、class、cdev、region |
| **shm_module** | `shm_init` | `vmalloc(SHM_BUF_SIZE)`、`alloc_chrdev_region`、cdev | `err_vfree` 等 | `shm_exit`：`vfree(g_shm)` |
| **myled** | `myled_probe` | `devm_kzalloc`、`devm_ioremap_resource`（或 simulated） | probe 失敗直接 `return ret`；已 devm 的由核心回收 | `myled_remove`：sysfs、`hw_shutdown`、`pm_runtime_disable` |
| **ssd** | `main` → `ftl_init` 等 | `calloc` mapping/NAND/SQ/CQ/RQ | `ftl_init` 失敗時 `ftl_destroy` 部分路徑 | `main` `out:`：`ftl_destroy`、`nvme_controller_destroy`、`request_queue_destroy` |
| **fwsh** | `shell_init` | 僅註冊 signal、readline | 無動態核心資源 | `shell_cleanup`：釋放 history、`rl_clear_history` |
| **scheduler** | `main` → `load_processes` | 靜態陣列 `proc[]`、`gantt[]` | stdin 格式錯誤 `exit(1)` | 行程結束即釋放（無 heap 模擬器狀態） |

**橫向結論**：

- **Kernel 教學範本**：chardev 的 `err_*` 鏈最適合講「驅動 init 必須可逆」；myled 的 **devm_*** 適合講「probe 失敗少寫 cleanup」。
- **User 模擬器**：ssd 明確成對 `*_init` / `*_destroy`；scheduler 幾乎無動態配置，與 ssd 的「可調 geometry」形成對比。

---

### 9.3 資料進出與 user/kernel 邊界

| 函式族 | 位置 | 資料怎麼動 | 每筆訊息 copy 次數（概念上） |
|--------|------|------------|------------------------------|
| `chardev_read` / `chardev_write` | kernel | `copy_to_user` / `copy_from_user` → 固定 4KB `drv.buf` | 各 1 次（user↔kernel） |
| `mq_write` / `mq_read` | kernel | user → 核心暫存 `kb[]` → `kfifo_in` / `kfifo_out` → user | **2 次**（進+出） |
| `shm_write` / `shm_read` | kernel | user → ring slot → user（syscall 路徑） | **2 次** |
| `mmap` + `mmap_worker` | kernel 建映射；user 寫 | user 直接 `memcpy` `shm->data[]` | **0 次** syscall 級 copy（仍有 user 內 memcpy） |
| `ftl_handle_write` | user 模擬 | 無 user 邊界；模擬 NAND program | N/A |
| `myled_reg_read/write` | kernel | MMIO `readl/writel` 或 `sim_regs[]` | 無 user copy（sysfs 字串另論） |
| `execute_pipeline` → `execvp` | user | 標準 I/O、pipe 位元組流 | 由子行程 libc 處理 |

**何時該用哪種（對照本 repo，非通則）**：

- **高頻小訊息、可共享緩衝區** → 參考 shm **mmap** 路徑（需自行補同步）。
- **生產者/消費者速率差大、要背壓睡眠** → 參考 **mq** 的 `wait_event` + `kfifo`。
- **單次配置、低頻** → **sysfs**（myled）或 **ioctl**（chardev）即可。
- **大塊韌體映像、順序讀寫** → chardev 式 buffer 或 mmap 檔案更直覺（本 repo chardev 為小 buffer 示範）。

---

### 9.4 佇列與環形緩衝區（滿/空判斷與背壓）

| 實作 | 檔案 | 滿條件 | 空條件 | 滿時行為 |
|------|------|--------|--------|----------|
| NVMe SQ | `nvme.c` | `sq_count == sq_capacity` | `sq_count == 0` | `nvme_submit_write` 回 **-1**；上層 drain pipeline |
| Request Q | `request.c` | `size == capacity` | `size == 0` | `enqueue` 回 false → `issue_pending` 停 |
| NVMe CQ | `nvme.c` | `cq_count == cq_capacity` | `cq_count == 0` | `nvme_post_completion` 回 false |
| kfifo MQ | `mq_module.c` | `kfifo_avail < MSG_SIZE` | `kfifo_len < MSG_SIZE` | write：**睡眠** `wait_event_interruptible` |
| SHM ring | `shm_module.c` / `common.h` | `(head+1)%CAP == tail` | `head == tail` | syscall write：**-ENOSPC**；mmap user：**spin** |
| RR ready Q | `scheduler.c` | （邏輯滿由 workload 決定） | `q_head == q_tail` | 時間前進到下一 arrival |

**搭配關係（ssd 一條龍）**：

```text
nvme_submit_write → SQ
nvme_issue_pending → RQ
scheduler_run → FTL → nvme_post_completion → CQ
nvme_reap_completions
```

面試時可對比：**ipc 用 sleep 處理背壓；ssd 用同步 drain；shm mmap 用 busy-wait（benchmark 專用，不建議 production）。**

---

### 9.5 同步原語橫向對照

| 原語 | 使用處 | 臨界區內容 | 能否睡眠 | 典型風險 |
|------|--------|------------|----------|----------|
| `mutex` | chardev buffer；mq `kfifo_in/out` | copy、改 buffer 長度 | 是 | 持鎖時不可呼叫會睡眠的 API |
| `spinlock` | shm syscall；myled MMIO | 短：index、readl/writel | 否（syscall 上下文仍應極短） | 持鎖過長、與 IRQ 同鎖死鎖（本 repo myled 無 IRQ handler） |
| `wait_queue` | mq 讀寫 | 等待 kfifo 空間/資料 | 在 wait 點睡眠 | 忘記 `wake_up` → 永久睡眠 |
| `spin_lock_irqsave` | myled reg | MMIO + sim_regs | 設計上可防 IRQ 同鎖（現無 ISR） | 與 sysfs 併發仍靠鎖序列化 |
| `atomic_t` / `atomic64_t` | chardev 計數；ipc 統計 | 單一計數器 | N/A | 無法保護與 buffer 相關的非原子欄位 |
| `smp_wmb` / `smp_rmb` | shm syscall | publication order | N/A | mmap user 路徑靠 `__sync_synchronize`，兩套語意需一致 |
| （無鎖） | ssd 全模擬；ipc mmap benchmark | — | — | 多執行緒下未定義行為 |

**選型口訣（結合本 codebase）**：

- **會觸發 page fault 的路徑**（`copy_from_user`）→ 用 **mutex**（chardev、mq）。
- **極短、僅改 index** → 可 **spinlock**（shm syscall）；若改 mmap 多執行緒，應改 **atomics** 而非沿用 spinlock。
- **等資料/等空間** → **waitqueue**（mq），不要用 spin 死等（除非像 benchmark 刻意量測）。

---

### 9.6 控制平面：ioctl vs sysfs vs proc

| 介面 | 代表函式 | 參數形式 | 本 repo 用途 |
|------|----------|----------|--------------|
| **ioctl** | `chardev_ioctl` | 結構化 cmd + 指標 | `RESET_BUF`、`GET_LEN`、`SET_RDONLY` |
| **sysfs** | `brightness_store` 等 | 單一文字屬性 | myled 硬體參數；chardev `read_only` |
| **proc** | `proc_show` / `mq_stats_show` | 人類可讀全文 | 除錯 dump；ipc/mq 統計 |

| 比較維度 | ioctl | sysfs | proc |
|----------|-------|-------|------|
| 腳本友善 | 需專用程式 | `echo`/`cat` 即可 | `cat` 即可 |
| 結構化 binary 資料 | 適合 | 不適合 | 不適合 |
| 穩定 ABI 壓力 | ioctl 編號需謹慎 | 屬性名較易擴充 | 較偏 debug |
| 本專題典型 | chardev 控制 | myled + chardev 開關 | 狀態/統計快照 |

**何時搭配**：chardev 同時三種都有——教學上可說：**資料面**用 read/write，**低頻開關**用 sysfs/ioctl，**除錯**用 proc。

---

### 9.7 「排程／調度」一詞在三處的不同意思

| 名稱 | 檔案 | 實際做什麼 | 是否 OS scheduler |
|------|------|------------|-------------------|
| `scheduler_run` | `ssd-fw-sim/scheduler.c` | 從 RQ 取 request、跑 FTL、填 CQ 時間戳 | 否（韌體 dispatch） |
| Linux CFS | （未實作） | — | — |
| `sched_fcfs` / `sched_rr` 等 | `cpu-scheduling-qemu/scheduler.c` | 離散時間模擬 CPU 分配 | 否（演算法 sandbox） |

避免混淆：**只有 `cpu-scheduling-qemu` 在算 AWT/ATT；`ssd-fw-sim` 的 scheduler 是 storage path 的 request 調度。**

---

### 9.8 錯誤回傳與傳播（同為「失敗」，回應不同）

| 場景 | 代表行為 | 上層如何知道 |
|------|----------|--------------|
| chardev write 唯讀 | 回 `-EACCES` | user errno |
| chardev ioctl 非法 cmd | `-ENOTTY` | user errno |
| shm ring 滿（syscall） | `-ENOSPC` | benchmark retry loop |
| mq read 無資料（nonblock） | `-EAGAIN` | user retry |
| nvme SQ 滿 | `nvme_submit_write` → -1 | `replay_trace` drain 後重試 |
| FTL 失敗 | `ftl_handle_request` false | `NVME_STATUS_INTERNAL_ERROR` completion |
| `ssd_config_validate` 失敗 | `main` return 1 | 程式退出碼 |
| scheduler stdin 錯 | `exit(1)` | 腳本中斷 |

**設計對照**：kernel 模組多依 **負 errno**；ssd 模擬器用 **bool + 統計**；排程器用 **exit code**。

---

### 9.9 觀測與量測函式（除錯時先找誰）

| 函式 / 介面 | 模組 | 輸出什麼 |
|-------------|------|----------|
| `stats_print` / `stats_export_csv` | ssd | WA、GC 次數、queue/service latency |
| `/proc/mq_stats`、`/proc/shm_stats` | ipc | enqueue/dequeue、avg_latency_ns、fifo/ring 使用量 |
| `/proc/chardev_info` | chardev | buffer 內容、計數 |
| `pr_info` / `dev_info` / `LOG_*` | 多處 | 即時追蹤 |
| `print_results` + `BENCHMARK` 行 | cpu-scheduling | AWT/ATT/ART |
| `cat info`（sysfs） | myled | 暫存器快照、是否 simulated |

**橫向建議**：效能問題先 **量化介面**（stats/proc），再 **dmesg**；行為問題先 **strace**（user）或 **縮小測例**（ssd trace）。

---

### 9.10 函式「搭配鏈」對照（面試畫圖用）

**Kernel 裝置一條龍（chardev / ipc 類似）**：

```text
module_init → alloc_chrdev_region → cdev_add → device_create
→ [runtime] vfs_open → fops.read/write/ioctl
→ module_exit → 反向拆除
```

**Platform 驅動（myled）**：

```text
module_platform_driver → myled_probe → devm_* + myled_hw_init
→ sysfs_create_group → [runtime] store/show → myled_reg_*
→ myled_remove → sysfs_remove + hw_shutdown
```

**SSD 寫入（無 kernel）**：

```text
main → ftl_init / nvme_init
→ replay_trace → service_nvme_pipeline
     → issue → scheduler_run → ftl_handle_write → [gc_run] → post_completion → reap
→ stats_print → *_destroy
```

**Shell 執行外部指令**：

```text
parse_line → execute_pipeline
→ [builtin?] exec_builtin
→ [否] pipe + fork + dup2 + execvp → waitpid / SIGCHLD
```

---

### 9.11 次要但常見的 helper（角色一句話）

| 函式 | 模組 | 角色 |
|------|------|------|
| `nvme_request_from_submission` | ssd | SQ entry → `request_t` |
| `mapping_set_physical_page` | ssd | 更新 L2P |
| `nand_invalidate_page` | ssd | 舊頁標 invalid，餵 GC |
| `gc_select_victim_block` | ssd | 選 invalid 最多的 block |
| `free_block_pool_push/pop` | ssd | 回收/取得可寫 block |
| `ssd_config_validate` | ssd | 啟動前擋掉非法 geometry |
| `single_open` + `seq_read` | chardev/ipc proc | proc 讀取樣板 |
| `vmalloc_to_pfn` + `remap_pfn_range` | ipc | 非連續實體頁映射到 user |
| `is_builtin` / `exec_builtin` | fwsh | 表驅動內建指令 |
| `gantt_push` | cpu-scheduling | 合併 Gantt 區間 |
| `of_property_read_u32` | myled | 從 DT 讀 `num-leds` 等 |

這些函式單獨看不難，**面試價值在能說清它在鏈上的位置與替換後果**（例如改掉 `gc_select_victim_block` 策略對 WA 的影響）。

---


## 十、開發挑戰、Bug 與除錯建議

本章依**各子專題實際程式碼**整理：開發時容易踩到的坑、可重現的實作限制，以及建議的排查順序。本儲存庫**普遍沒有硬體 ISR/DMA**，除錯重心多在：模組載入、user/kernel 邊界、同步、佇列背壓、腳本環境與模擬器狀態機。

### 10.1 共通環境問題

| 現象 | 常見根因 | 建議排查（由外而內） |
|------|----------|----------------------|
| `insmod` / `Invalid module format` | 模組用錯 kernel 版本編譯 | `uname -r` 與 `ls /lib/modules/$(uname -r)/build` 是否存在；在該模組目錄 `make clean && make` 後重載 |
| `insmod` 成功但無 `/dev/*` | udev 未跑、或 `device_create` 失敗 | `dmesg \| tail -30`；`ls -l /dev/mq_ipc /dev/shm_ipc /dev/chardev0`；`lsmod` 確認模組在 |
| `Operation not permitted` 開裝置 | 非 root、或權限未 chmod | ipc 的 `01_setup.sh` 會 `chmod 666`；chardev 的 `load.sh` 亦同；手動 `sudo chmod 666` 對照 |
| 腳本 `set -e` 中途退出 | 前置步驟失敗（apt、編譯、路徑） | 不要只看最後一行；往上找第一個 `[ERR]` / 非零 exit；單獨手動執行失敗的那一行 |
| Secure Boot 阻擋模組 | 主機政策 | `mokutil --sb-state`；教學環境可暫關或簽 module（本 repo 未提供簽章流程） |

**chardev `load.sh` 注意**：`sudo insmod ... \|\| true` 會**吞掉 insmod 失敗**，後續仍可能印 `[+] Done.`。若行為異常，應直接執行 `sudo insmod driver/chardev.ko` 看真實錯誤碼，並查 `dmesg`。

---

### 10.2 `ssd-fw-sim`（user space，無 root）

#### 開發挑戰

- **多層佇列背壓**：SQ 滿、RQ 滿、CQ 滿、`scheduler_run` 失敗會在不同層級中止；只看最後 `Trace replay failed` 不足以定位。
- **GC 與空間**：`gc_free_block_threshold` 設太小或 workload 過重會拉高 `foreground_gc_count`，延遲飆升。
- **設定與 geometry**：`logical_pages` 大於 `total_blocks × pages_per_block` 時 `ssd_config_validate` 直接失敗（`config.c`）。

#### 已知實作限制（非偶發 bug，但面試/除錯要會講）

| 項目 | 程式碼依據 |
|------|------------|
| 僅支援 WRITE trace | `replay_trace` 非 `WRITE` 行 `continue`（`main.c`） |
| 無 READ / trim | `ftl_handle_request` 僅處理 `REQUEST_TYPE_WRITE` |
| 單執行緒、全域 `g_ftl` | 無鎖；不可並行跑兩個模擬實例 |
| Wear leveling API 未接 GC | `free_block_pool_get_min_erase_block` 無呼叫者 |
| CQ reap 不驗 phase | `nvme_reap_completions` 只移動 head/count |

#### 症狀 → 排查表

| 症狀 | 優先檢查 | 指令 / 手法 |
|------|----------|-------------|
| `Trace replay failed` | pipeline 是否回 false | 暫設 `g_log_level = LOG_LEVEL_DEBUG`（`common.h`）；重跑 trace |
| `Trace request out of logical range` | LBA+size 與 `logical_pages` | 對照 trace 與 `ssd.conf`；`ssd_config_print` 輸出 |
| `Out of NAND space`（log） | GC 後仍 allocate 失敗 | 看統計 `Foreground GC Count`、`GC Count`；調高 `total_blocks` 或 threshold |
| `NVMe submission queue enqueue failed after draining` | FTL/CQ 持續失敗 | 查是否 `nvme_post_completion` 失敗（CQ 滿）；`replay_trace` 內有額外 reap（L99-104） |
| 延遲統計怪異（負數理論上不該出現） | 時間軸 | `scheduler_run` 會把 `current_time_us` 對齊 `submit_timestamp_us`；對照 `tests/test_suite.c` 的 latency 會計 assert |
| 改 `ssd.conf` 無效 | 是否有傳 `--config` | 預設只 `ssd_config_init_default`；需 `./ssd_fw_sim --config path trace` |
| WA 異常高 | GC migrate 次數 | `Migrated Pages`、`nand_write_count` vs `host_page_count` |

#### 建議除錯流程（一次跑通）

```bash
cd ssd-fw-sim
make test                    # 先確認 regression 全過
./ssd_fw_sim traces/sample.trace
# 若失敗：改小 geometry 的 conf，或先用 sample.trace
./ssd_fw_sim --csv /tmp/out.csv traces/sample.trace
```

用 CSV 欄位對照：`gc_count`、`migrated_pages`、`wa`（見 `stats_export_csv`）。若單元測試過、trace 不過，問題多半在 **trace 格式或 LBA 範圍**，而非 FTL 核心邏輯。

#### 間歇性問題

此模擬器為**決定性單執行緒**，同一 trace + 同一 config 應可重現。若結果不同，優先懷疑：**trace 檔被改**、**用了不同 `--config`**、或 **未 `make clean` 連到舊 binary**。

---

### 10.3 `chardev-driver`（kernel module）

#### 開發挑戰

- **三條路徑改 `read_only`**：`ioctl SET_RDONLY`、`sysfs read_only_store`、與 `chardev_write` 檢查——**後兩者與 write 的 mutex 範圍不一致**（見下）。
- **proc 洩漏 buffer 內容**：`proc_show` 用 `%.*s` 印出 kernel buffer，除錯方便，但若 buffer 含敏感資料需注意。
- **卸載順序**：`chardev_exit` 先 `proc_remove` 再 `device_destroy`；若 user 仍開著 fd，可能影響卸載（教學環境少見，實務要測）。

#### 程式碼層級問題（除錯時可主動提出）

| 問題 | 位置 | 說明 |
|------|------|------|
| `read_only` race | `chardev_write` L98-106 vs `ioctl` L142-147 | write 在 mutex **外**讀 `read_only`；ioctl 改旗標無鎖 → 可能「檢查通過後仍被設唯讀」的窗口 |
| `IOCTL_GET_LEN` 無 mutex | `chardev_ioctl` GET_LEN | 與並行 write 併發時 `buf_len` 可能不一致（統計用途影響較小） |
| `copy_to_user` 部分失敗 | `chardev_read` L83-91 | `not_copied != 0` 時仍推進 `*ppos` 並回傳部分長度，未回 `-EFAULT`（與嚴格 VFS 語意有落差） |
| write 覆寫語意 | `chardev_write` L108-110 | 總是從 buffer 開頭寫入並設 `buf_len`，非 append；`test_app` 用 `lseek`+`read` 驗證 |
| 錯誤碼 | L100-101 註解 | 曾考慮 `-EPERM`，實際回 `-EACCES`；user 端 `test_app` 用 `%m` 看 errno |

#### 症狀 → 排查表

| 症狀 | 手法 |
|------|------|
| `open` 失敗 ENOENT | 模組未載入或無 node：`lsmod \| grep chardev`；`ls /dev/chardev0`；重跑 `sudo ./scripts/load.sh` |
| write 有時成功有時 EACCES | 並行 `ioctl`/`sysfs` 與 write：`stress` 腳本交替 `echo 1 > read_only` 與 `write` |
| read 資料不對 | 是否忘記 `lseek(0)`；write 後 `*ppos` 在尾端 |
| `cat /proc/chardev_info` 與 sysfs 不一致 | 兩者讀同一 `drv`，但時序不同；在單執行緒下應一致 |
| 卸載後 device 還在 | `unload.sh` 會印 WARNING；手動 `sudo rmmod chardev` 再看 `dmesg` |

#### 建議工具

```bash
# Terminal 1
sudo dmesg -w | grep chardev

# Terminal 2
strace -e open,read,write,ioctl ./userspace/test_app
ls -l /sys/class/chardev/chardev0/
cat /proc/chardev_info
```

---

### 10.4 `linux-ipc-benchmark`（kernel + pthread）

#### 開發挑戰

- **layout 必須一致**：kernel `struct shm_region`（`shm_module.c`）與 user `shm_region_t`（`common.h`）大小/對齊不一致會導致 mmap 後寫壞記憶體（症狀難查）。
- **三條路徑語意不同**：MQ 滿時 **睡眠**；SHM syscall 滿時 **`-ENOSPC`**；mmap 滿時 user **busy-spin**（`benchmark.c` L92-95）。
- **延遲統計語意**：`/proc/mq_stats` 的 `avg_latency_ns` 用「最近一次 enqueue 到本次 dequeue」估算（`mq_module.c` L130-132），**不是**每筆訊息精確配對，解讀 benchmark 時勿過度延伸。

#### 已知限制與風險

| 項目 | 說明 |
|------|------|
| mmap 無 kernel 鎖 | producer/consumer 直接改 `head`/`tail`；`benchmark` 假設 **SPSC**（單 producer 單 consumer） |
| `shm_read` 要求 `len >= MSG_SIZE` | 否則 `-EINVAL`（L135） |
| `mq_read` 同理 | `len < MSG_SIZE` → `-EINVAL` |
| Test 3 高 CPU | spin-wait 會吃滿核心；吞吐高不等於「省 CPU」 |
| `benchmark` 結尾 `system("cat /proc/...")` | 方便 demo，自動化基準應只解析程式自身輸出 |

#### 症狀 → 排查表

| 症狀 | 手法 |
|------|------|
| `open /dev/mq_ipc` 失敗 | 未跑 `sudo bash scripts/01_setup.sh`；或已 `04_cleanup` → 重載模組 |
| `benchmark` Test 1/2 極慢 | 正常比 Test 3 慢；若 Test 2≈Test 1，表示瓶頸在 copy 而非 kfifo vs spinlock |
| Test 3 吞吐異常低 | `top` 看是否雙核 spin；確認 Test 3 前 `shm->head=tail=0`（`benchmark.c` L222） |
| `read: Invalid argument` | read buffer 小於 64 bytes |
| `write: No space left on device`（SHM） | ring 滿；consumer 沒跑或太慢 |
| mq 永遠阻塞 | consumer 未啟動；或 `wait_event` 被 signal 打斷（`-EINTR`） |
| insmod 後 dmesg 有錯 | `modinfo kernel/mq_module.ko`；確認 kernel 版本 |

#### 建議除錯流程

```bash
cd linux-ipc-benchmark
sudo bash scripts/01_setup.sh
# Terminal A
watch -n 1 'echo === mq ===; head -5 /proc/mq_stats; echo === shm ===; head -5 /proc/shm_stats'

# Terminal B
cd user && ./benchmark 10000   # 先用較小 count 縮短迭代

# 單獨驗證 mmap 語意
./shm_demo
```

比對三次測試的 **wall msg/s** 與 proc 中 `avg_latency_ns`（僅作趨勢，非嚴格 per-message latency）。

---

### 10.5 `fwsh`（user space）

#### 開發挑戰

- **pipe + fork 語意**：父行程必須關閉所有 pipe fd（`executor.c` L204-207）；否則下游 `read` 永遠等 EOF。
- **fork 失敗處理不完整**：`fork` 失敗時 `break` 離開迴圈，但已 fork 的子行程與已開 pipe 的清理路徑有限（L154-157），複雜 pipeline 下可能留下殭屍或洩漏 fd（壓力測試時較易見）。
- **signal handler 與 readline**：`SIGINT` 內呼叫 readline 函式，嚴格 POSIX 下並非全部 async-signal-safe；若遇極端 crash，可改為 main loop 處理 signalfd（目前程式未實作）。

#### 症狀 → 排查表

| 症狀 | 手法 |
|------|------|
| `cmd1 \| cmd2` 卡住 | `strace -f -e pipe,fork,read,write,close ./fwsh`；確認 parent/child 都 `close` pipe 兩端 |
| `cd` 無效 | 是否在 pipeline 裡跑 `cd`（pipeline 內 cd 在子 process，不影響父 shell） |
| 背景指令變殭屍 | 查 `SIGCHLD` 是否註冊；`ps aux \| grep defunct`；`shell.c` 的 `waitpid(-1, NULL, WNOHANG)` |
| Tab/顏色錯亂 | prompt 內 `\001...\002` 邊界（`shell.c` `build_prompt`） |
| `command not found` 127 | 子行程 `execvp` 失敗；PATH 問題 |
| memory leak | `make valgrind` 或 `make debug`（Makefile 目標） |

---

### 10.6 `qemu-platform-demo`

#### 開發挑戰

- **腳本鏈長、耗時**：kernel 編譯、DTB overlay、BusyBox 路徑任一失敗都會拖到後續；錯誤訊息可能在十幾分鐘前。
- **simulated 模式**：MMIO 無回應或 VERSION 不符時自動走 `sim_regs`（`myled_hw_init`）；sysfs 仍可用，但與「真實 readl」行為不同——除錯時看 `cat info` 的 `simulated` 欄位。
- **sysfs 路徑**：裝置名稱依 DT，常見 `10010000.myled-controller`；找不到時在 QEMU 內 `find /sys -name enable`。

#### 症狀 → 排查表

| 症狀 | 手法 |
|------|------|
| `01_build_kernel.sh` 失敗 | 查磁碟空間、網路下載、`bc/bison/flex`；單獨重跑該腳本 |
| `04_build_rootfs.sh` 失敗 | 腳本內 `BUSYBOX` 變數是否指向本機 busybox；`0A_fix_busybox_arch.sh` 是否需跑 |
| QEMU 內無 `/test_myled.sh` | rootfs overlay 是否打包進 initramfs |
| `insmod myled_ctrl.ko` 失敗 | 是否用 **ARM64** 建出来的 ko（`03_build_driver.sh`）；vermagic 對照 guest `uname -r` |
| 寫 brightness 無效 | `cat info` 是否 `simulated=yes`；dmesg 是否有 `no HW response` |
| 離開 QEMU 卡住 | `Ctrl-A` 放開再 `x`（README_platform 寫法） |

---

### 10.7 `cpu-scheduling-qemu`

#### 開發挑戰

- **名稱易誤解**：此專題**不是**改 Linux kernel scheduler；問題若出在「guest 內排程怪」應先排除——實際量的是 **host 編譯的 `scheduler` 二進位** 吃 stdin workload。
- **腳本依賴網路**：`01_setup_env.sh` 下載 cloud image；proxy/磁碟滿會失敗。
- **RR 邊界**：`sched_rr` 入隊時機與 re-enqueue 順序影響結果；若 benchmark 與手動跑不一致，用同一 `workload_*.txt` 對照。

#### 症狀 → 排查表

| 症狀 | 手法 |
|------|------|
| `01_setup_env.sh` 很慢/失敗 | 檢查 `wget`、映像路徑 `vm/`；磁碟空間 |
| SSH 2222 連不上 | VM 是否起來：`02_start_vm.sh` 輸出；`ss -lntp \| grep 2222` |
| `03_demo` 空輸出 | `results/demo_output.txt`；手動 `./scheduler fcfs < src/workload_demo.txt` |
| benchmark 無法解析 | 輸出需含 `BENCHMARK` 行（`scheduler.c` `print_results`）；腳本 grep 是否匹配 |
| 數字與課本不同 | 檢查 time quantum（`rr` 第二參數）；SRTF 為離散 1 tick 搶占 |

---

### 10.8 橫向：記憶體、競態、觀測工具

| 目標 | 建議 |
|------|------|
| user 記憶體錯誤 | `fwsh`: `make debug`（ASan）；`benchmark`: 用較小 count 反覆跑 |
| kernel 記憶體錯誤 | 需自行用帶 `CONFIG_KASAN` 的 kernel 編譯模組（**本 repo 未附 KASAN 設定**） |
| 競態 | chardev `read_only`、ipc mmap `head`/`tail`——用 `stress`/`pthread` 並行重現 |
| 效能 | ipc：`perf stat ./benchmark`；ssd：看 `stats` 與 CSV，非 perf |
| 模組生命週期 | `refcnt` 異常時 `lsmod`；卸載前關閉所有開啟 `/dev` 的 process |

**無 ISR 的除錯取向**：本 repo 幾乎所有邏輯在 **process context**（syscall、sysfs store、模擬器主迴圈）。不必先查 IRQ handler；應查 **鎖、佇列滿/空、copy 失敗、初始化順序**。

---

### 10.9 建議的「從現象到根因」五步

1. **固定環境**：記錄 `uname -a`、是否 root、哪個子目錄、哪條命令。  
2. **縮小復現面**：ssd 用 `sample.trace` + `make test`；ipc 用 `10000` 筆；chardev 用 `test_app`。  
3. **分層日誌**：kernel → `dmesg`；user → stderr / `LOG_*`；腳本 → 不加 `|| true` 重跑。  
4. **對照統計介面**：`/proc/*_stats`、`stats_print`、`/proc/chardev_info`。  
5. **改一個變因**：只改 config 一項或只換一條 code path（例如 ipc 只跑 Test 2）。

---

### 10.10 快速對照表（面試／on-call 用）

| 模組 | 第一個該看的 | 第二個該看的 |
|------|--------------|--------------|
| ssd-fw-sim | `stats_print` / stderr log | `make test` 是否過 |
| chardev | `dmesg` + `/proc/chardev_info` | `strace test_app` |
| ipc | `/proc/mq_stats` & `shm_stats` | 三項 benchmark 相對比例 |
| fwsh | `strace -f` pipeline | 是否背景/`&` 殭屍 |
| myled | guest `dmesg` + `cat info` | 是否 simulated |
| cpu-scheduling | 手動 `./scheduler alg < workload` | `results/*.txt` |

---


## 十一、未來延伸探討議題

以下議題皆從**現有程式碼已留的鉤子**或**各模組明確未實作之處**出發，便於寫進履歷「後續工作」或面試時討論 tradeoff。標註「本 repo 現狀」者表示目前源碼中尚未完成。

---

### 11.1 全專題共通

| 議題 | 本 repo 現狀 | 可延伸方向 | 預期學習點 |
|------|--------------|------------|------------|
| **可觀測性統一** | proc/sysfs/dmesg/stdout 混用 | 為每模組加 tracepoint 或固定 debugfs layout | 降低 on-call 認知負擔 |
| **CI 矩陣** | 各子專題手動腳本 | GitHub Actions：編譯 + `ssd_fw_sim_tests` + 模組 smoke（需 kernel header 容器） | 回歸不只靠本機 |
| **KASAN/UBSAN** | 未內建 kernel 設定；fwsh 有 `make debug` | 用帶 KASAN 的 kernel 編譯 chardev/ipc；或擴充 ssd 的 sanitizer 測試 | 提早抓 OOB / UAF |
| **PREEMPT_RT** | 一般 mutex/spinlock | 在 RT kernel 上量測 myled MMIO 臨界區與 mq 延遲 | 理解「spinlock 在 process context 也可能搶占」 |
| **eBPF 觀測** | ipc 僅模組內 `ktime` 平均 | `bpftrace` 掛 `tracepoint:syscalls:sys_enter_write` 對 `/dev/mq_ipc` 量延遲分佈 | 不改模組即可 profiling |
| **文件與 ABI** | `common.h` 與 kernel struct 需手動同步 | 單一 header 產生器或 static assert 比對 `sizeof` | 避免 mmap layout 漂移 |

---

### 11.2 `ssd-fw-sim`

| 議題 | 依據 | 延伸作法 |
|------|------|----------|
| **Wear leveling** | `nand_block_t.erase_count`、`free_block_pool_get_min_erase_block` 已存在但 GC 未使用 | 在 `gc_select_victim_block` 混合 invalid 比例與 erase count 權重 | 平衡 WA 與壽命 |
| **READ / TRIM trace** | `ftl_handle_request` 僅 WRITE | 加 read path、`read_latency_us` 統計、可選不觸發 GC | 完整 FTL 教學模型 |
| **NVMe phase 驗證** | `cq_phase` 有設定，reap 未檢查 | host 側模擬 poll 邏輯 | 貼近真實協定 |
| **多執行緒 host** | 全域 `g_ftl`、無鎖 | 每 thread 一組 SQ/RQ 或細粒度鎖 | 並發 host 壓力測試 |
| **斷電一致性** | 映射表僅 RAM | journal + checkpoint（`ssd-fw-sim/README` future 列表已有方向） | 韌體停電恢復思維 |
| **Victim 策略** | 僅 greedy max-invalid | hot/cold 分離、over-provisioning 參數化 | 論文級 WA 調教 |
| **多 channel / die** | 單一 `current_write_block` | 並行 program/erase 模型 | 吞吐上限分析 |
| **與真實 trace 對接** | `gen_trace.py` 產生簡單 WRITE | 匯入 block trace 子集（如 msr 格式子集） | 閉環驗證 |

---

### 11.3 `chardev-driver`

| 議題 | 依據 | 延伸作法 |
|------|------|----------|
| **修復 read_only race** | write 與 ioctl/sysfs 不同步 | 單一 mutex 保護 `read_only` + buffer | 正確性 |
| **poll / epoll** | 無 `poll` fops | 實作 `poll_wait` + 資料就緒 flag | 非阻塞應用 |
| **多 minor 裝置** | `alloc_chrdev_region(..., 1, ...)` 僅 1 個 | 擴充 minor 陣列、per-device `drv` |  scalability 教學 |
| **DMA / scatter-gather** | 純 kmalloc buffer | `dma_alloc_coherent` 環形緩衝（若接假硬體） | 與 IPC 對照 copy 成本 |
| **devm 化** | 手動 `kfree` buffer | 改 `devm_kzalloc` 綁 device | 與 myled 風格統一 |
| **嚴格 VFS 語意** | read 部分 copy 處理 | copy 失敗回 `-EFAULT`、不推進 `ppos` | 合規驅動 |

---

### 11.4 `linux-ipc-benchmark`

| 議題 | 依據 | 延伸作法 |
|------|------|----------|
| **mmap 同步** | user `__sync_synchronize` + kernel `smp_wmb` | 統一為 C11 `atomic_uint` + `memory_order_release/acquire` | 可移植 memory model |
| **MPSC / MPMC** | benchmark 假設 SPSC | 多 producer 測試 + 鎖或無鎖 queue | 釐清效能天花板 |
| **eventfd / io_uring** | 僅 read/write/mmap | 第四條路徑：blocking 與 batch | 現代 Linux IPC 對照 |
| **Huge pages** | `PAGE_ALIGN` 一般頁 | `MAP_HUGETLB` 實驗 TLB miss | 大環形緩衝區場景 |
| **延遲統計** | 以「最後一次 enqueue」估 latency | per-slot 時間戳環 | 準確 p99 |
| **seccomp / 命名空間** | 未涉及 | 容器內跑 benchmark 看隔離開銷 | 部署語境 |
| **與 POSIX MQ 對照** | 自製 kfifo 模組 | 同 workload 下 `mq_open` 系統 API 基準 | 凸顯「自幹 vs 系統」差異 |

---

### 11.5 `fwsh`

| 議題 | 依據 | 延伸作法 |
|------|------|----------|
| **Job control** | 背景僅印 PID | `jobs`/`fg`/`bg`、內建 job table | 完整 Shell |
| **環境變數展開** | parser 不處理 `$VAR` | 擴充 lexer | 腳本相容 |
| **here-doc / 腳本** | 僅互動 REPL | 讀檔批次執行 | 自動化測試 |
| **內建更多韌體工具** | 已有 crc32/hexdump/memmap | 加 `strings`、固定區段 checksum | 作品集深度 |
| **signalfd 取代 handler 內 readline** | SIGINT 內呼叫 readline API | 事件驅動 REPL | 信號安全 |
| **fork 失敗清理** | `fork` 失敗後 break，pipe 可能未全關 | 統一 rollback 路徑 | 穩健性 |

---

### 11.6 `qemu-platform-demo`

| 議題 | 依據 | 延伸作法 |
|------|------|----------|
| **IRQ + threaded IRQ** | 無 `request_irq` | DTS 加 interrupt；`threaded_irq` 處理按鍵 | 完整 bottom half |
| **Linux LED 子系統** | 自建 sysfs | 改 `led_classdev_register` + trigger | 核心標準介面 |
| **pm_runtime 深度** | 僅 enable + suspend/resume 骨架 | `pm_runtime_get/put` 與實際硬體 idle | 省電模型 |
| **多實例** | 單一 platform device | DTS 多節點、多 `myled_priv` | 驅動可擴展性 |
| **真實硬體移植** | simulated fallback | 將 DT 改到實板 base address | bring-up 實戰 |
| **devicetree 動態 overlay** | `fdtoverlay` 腳本 | 教學用 plugin manager / U-Boot overlay | 產線更新硬體描述 |

---

### 11.7 `cpu-scheduling-qemu`

| 議題 | 依據 | 延伸作法 |
|------|------|----------|
| **Context switch 成本** | 切換不計 overhead | 每次 preempt 加固定 penalty | 更貼近 OS |
| **Multi-core** | 單 CPU 模擬 | 每 core 一條 ready queue + migration | 負載平衡 |
| **Aging / 飢餓** | Priority 無動態調整 | 實作 aging（README_schedule 已列） | RR/Priority 公平性 |
| **與 guest 真實負載對照** | VM 內跑 stress 與 host 模擬器分離 | 在 VM 內用 `schedtool` 或 trace sched_switch | 理論與真實對照 |
| **圖形化** | 純文字 Gantt | 從 `BENCHMARK` CSV 畫圖 | 報告呈現 |
| **即時排程** | 離散 tick | 導入 EDF 模擬（需週期任務輸入格式） | RTOS 銜接 |

---

### 11.8 跨模組整合型專題（履歷級「大方向」）

這些**目前不存在於 repo**，但與現有六塊能力自然銜接，適合寫在「未來工作」：

1. **統一「韌體除錯 Shell」管線**  
   在 `fwsh` 內建子命令：載入 `chardev` 測試、`cat /proc/shm_stats`、觸發 `ssd_fw_sim` trace——把分散的 demo 收成一条 workflow。

2. **Storage + IPC 聯合基準**  
   用 `ssd-fw-sim` 產生寫入負載 log，同時跑 `linux-ipc-benchmark`，觀察系統在 CPU 與 memory 壓力下的 IPC 退化（需自行設計實驗，本 repo 無腳本）。

3. **QEMU 內跑 chardev/ipc 模組**  
   現有 myled 在 ARM64 initramfs；可嘗試 x86_64 最小 rootfs + 本 repo 的 x86 模組（需另建映像，**非現成腳本**）。

4. **從 myled MMIO 到「假 NVMe 暫存器」**  
   在 platform driver 上暴露一組類 NVMe doorbell 的 sysfs/MMIO，用 user 工具寫入後由 kernel 模組轉成對 `ssd-fw-sim` 的 trace 餵入——概念整合 storage 與 driver（研究向，工作量大）。

5. **教學用「對照表生成器」**  
   從各 `report_*.md` 與本 `report.md` §9 自動產生一頁 cheat sheet（工具鏈問題，非韌體核心）。

---

### 11.9 建議優先順序（若時間有限）

| 優先級 | 項目 | 理由 |
|--------|------|------|
| P0 | 修 chardev `read_only` 同步；ipc mmap 改 atomics | 低成本、正確性與面試故事明確 |
| P1 | ssd GC 接入 `get_min_erase_block`；加 READ trace | 直接強化作品集核心模組 |
| P2 | myled 改 `led_classdev` 或加 IRQ demo | 嵌入式 Linux 標準介面 |
| P3 | eBPF / CI / QEMU 整合 x86 模組 | 工程化與展示效果 |

---

### 11.10 與各子目錄文件的銜接

各子專題 `README_*.md` 末尾多已有簡短「未來擴充」列表；本章 §11.2–11.7 與之對齊並補上**程式碼依據**。實作前請以當時的 `*.c` 為準，避免文件與程式分叉。

---

## 十二、子報告索引

| 路徑 | 內容 |
|------|------|
| `ssd-fw-sim/report_ssd_api.md` | SSD API / 呼叫鏈 / 風險 |
| `chardev-driver/report_char.md` | 字元驅動細節 |
| `linux-ipc-benchmark/report_ipc.md` | IPC 實作 |
| `fwsh/report_fwsh.md` | Shell 模組 |
| `qemu-platform-demo/report_platform.md` | 平台驅動與 QEMU |
| `cpu-scheduling-qemu/report_schedule.md` | 排程模擬 |

---

*本報告依 repository 現有檔案整理；若程式碼變更，請以源碼為準重新驗證。*
