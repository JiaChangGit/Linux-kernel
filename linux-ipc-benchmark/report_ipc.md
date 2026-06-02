# Linux IPC 基準測試：設計、實作與除錯報告

本報告說明 `linux-ipc-benchmark` 的設計目的、核心資料路徑、效能觀察方式，以及開發中遇到的問題。內容以目前專案中的 `kernel/`、`user/`、`scripts/` 為準，不把外部 IPC 框架的行為套進來解釋。

---

## 1. 專案目標

這個專案要回答一個具體問題：

> 同樣傳 64 bytes 訊息時，Message Queue 與 Shared Memory 的成本差在哪裡？

為了讓差異可觀察，專案刻意實作三條路徑。正式 benchmark 只有這三種測試；`copy_from_user()`、`copy_to_user()`、`spinlock` 是 SHM syscall 的實作細節，不是第四種測試。

| 路徑 | 實作位置 | 說明 |
| --- | --- | --- |
| MQ syscall | `kernel/mq_module.c` | 使用 `kfifo` 保存訊息，user 透過 `write()` / `read()` 存取，每筆訊息有 user/kernel copy。 |
| SHM syscall | `kernel/shm_module.c` | 使用 kernel 端 `struct shm_region` ring，user 仍透過 `write()` / `read()` 存取；目前 `shm_write()` 在 `spinlock` 內做 `copy_from_user()`，這是已知風險。 |
| SHM mmap | `kernel/shm_module.c` + `user/common.h` | kernel 把 `vmalloc` pages 映射到 user VMA，user 透過 `shm_region_t` 直接讀寫 mapped page；每筆訊息不走 `read/write` syscall，也不走 `copy_from_user()` / `copy_to_user()`。 |

這三條路徑不是要證明哪一種 API 永遠最好，而是拆開觀察三個成本來源：

1. 資料複製次數（Data Copy Count）
2. 系統呼叫次數（System Call Count）
3. 同步方式（Synchronization Method）

---

## 2. 重要名詞

| 名詞 | English | 在本專案中的意思 |
| --- | --- | --- |
| IPC | Inter-Process Communication | 行程間交換資料的機制。 |
| Syscall | System Call | user program 進入 kernel 的入口，例如 `read()`、`write()`、`mmap()`。 |
| User/Kernel Boundary | 使用者/核心邊界 | 一般程式不能直接碰 kernel memory，需要 syscall 與安全檢查。 |
| Message Queue | 訊息佇列 | 先進先出（FIFO）的訊息傳遞模型。 |
| Shared Memory | 共享記憶體 | 多個執行路徑看到同一段記憶體，不靠 kernel 每筆轉送資料。 |
| Zero-copy | 零複製 | 避開 user/kernel 之間的 `copy_from_user()`、`copy_to_user()`。 |
| Ring Buffer | 環形緩衝區 | 固定大小陣列，透過 `head`、`tail` 重複使用 slot。 |
| Backpressure | 回壓 | 佇列滿時讓生產者等待，避免資料無限制累積。 |
| Busy Polling | 忙碌輪詢 | 一直檢查狀態直到可讀/可寫，延遲低但會消耗 CPU。 |
| Memory Barrier | 記憶體屏障 | 限制記憶體操作順序，避免 CPU 或編譯器重排造成讀寫錯誤。 |

---

## 3. 整體架構

```text
userspace
  ├─ mq_demo / benchmark
  │    └─ open/read/write /dev/mq_ipc
  │         └─ kernel/mq_module.c
  │              └─ kfifo + mutex + wait_queue
  │
  └─ shm_demo / benchmark
       └─ open/read/write/mmap /dev/shm_ipc
            └─ kernel/shm_module.c
                 ├─ syscall path: vmalloc ring + spinlock
                 └─ mmap path: remap kernel pages into user VMA
```

`/proc/mq_stats` 與 `/proc/shm_stats` 用來觀察 kernel module 的統計資料，例如 enqueue/dequeue count、write/read count、ring used slots、average latency。

```mermaid
flowchart TB
    subgraph MQ["1. MQ syscall"]
        MQP["producer"] --> MQW["write(/dev/mq_ipc)"]
        MQW --> MQCFU["copy_from_user()"]
        MQCFU --> KF["kfifo"]
        KF --> MQCTU["copy_to_user()"]
        MQCTU --> MQR["read(/dev/mq_ipc)"]
        MQR --> MQC["consumer"]
    end

    subgraph SHMS["2. SHM syscall"]
        SP["producer"] --> SW["write(/dev/shm_ipc)"]
        SW --> SL["spin_lock() + copy_from_user()"]
        SL --> KR["kernel shm_region data offset 136"]
        KR --> SR["memcpy(tmp) + copy_to_user()"]
        SR --> RR["read(/dev/shm_ipc)"]
        RR --> SC["consumer"]
    end

    subgraph SHMM["3. SHM mmap"]
        MP["producer"] --> UM["user shm_region_t data offset 192"]
        UM --> VP["mapped vmalloc pages"]
        VP --> MC["consumer"]
    end
```

---

## 4. 三條資料路徑

### 4.1 MQ syscall：`kfifo` + `write()` / `read()`

MQ 模組提供 `/dev/mq_ipc`。使用者程式寫入訊息時會進入 `mq_write()`；讀取訊息時會進入 `mq_read()`。

```text
producer user buffer
  -> write()
  -> mq_write()
  -> copy_from_user()
  -> kernel stack buffer
  -> kfifo

consumer user buffer
  <- read()
  <- mq_read()
  <- copy_to_user()
  <- kernel stack buffer
  <- kfifo
```

特性：

- `kfifo` 是 kernel 內建 FIFO 結構，適合用來實作固定大小的 queue。
- `mutex` 保護 `kfifo_in()` / `kfifo_out()`。
- `wait_queue` 讓 reader 在沒有資料時睡眠，writer 在空間不足時等待。
- 每筆訊息有 user -> kernel 與 kernel -> user 兩次資料複製。

這條路徑的好處是語意清楚：滿了可以等、空了可以等，呼叫端不用自己寫輪詢。但每筆訊息都要進核心，也要複製資料。

### 4.2 SHM syscall：kernel ring 但仍使用 `read()` / `write()`

SHM 模組提供 `/dev/shm_ipc`。它底層使用 `vmalloc()` 配置一份 kernel 端 ring buffer，但 syscall path 仍透過 `shm_write()` / `shm_read()` 搬移資料。

```text
producer user buffer
  -> write()
  -> shm_write()
  -> copy_from_user()
  -> g_shm->data[head]
  -> head = next

consumer user buffer
  <- read()
  <- shm_read()
  <- copy_to_user()
  <- tmp buffer
  <- g_shm->data[tail]
```

特性：

- `spinlock` 保護 `head`、`tail` 與 ring slot。
- ring full 時 `shm_write()` 回傳 `-ENOSPC`。
- ring empty 時 `shm_read()` 回傳 `-EAGAIN`。
- 仍有兩次 user/kernel 資料複製。
- 目前 `copy_from_user()` 在 `spin_lock()` 之後執行，這個設計有不可睡眠 context 的風險，適合當教學對照，不應描述成 production-ready 寫法。

這條路徑的用途是做對照：底層換成 kernel 端 ring buffer 之後，如果還是每筆訊息都 syscall 並複製資料，效能不會只因為名字叫 shared memory 就自然變快。

### 4.3 SHM mmap：共享頁面 + user-space ring

`mmap()` 路徑只在建立映射時進入 kernel。映射完成後，使用者程式直接操作 `shm_region_t`。

```text
setup:
  open("/dev/shm_ipc")
  mmap(...)
    -> shm_mmap()
    -> vmalloc_to_pfn()
    -> remap_pfn_range()

runtime:
  producer writes shm->data[head]
  producer updates shm->head.value
  consumer reads shm->data[tail]
  consumer updates shm->tail.value
```

特性：

- 每筆訊息不再呼叫 `read()` / `write()`。
- 每筆訊息不走 `copy_from_user()` / `copy_to_user()`。
- producer 仍會用 `memcpy()` 把資料寫進 mapped page，consumer 也會用 `memcpy()` 取出快照；這裡的 zero-copy 只是不跨 user/kernel 邊界複製。
- producer 與 consumer 要自己處理 full/empty 判斷。
- 需要 memory barrier，確保資料寫入發生在 `head` 更新之前。
- 目前使用 `volatile` 與 `__sync_synchronize()`，不是完整 C11 atomic SPSC queue，也不支援多 producer / 多 consumer。

這條路徑的重點是 zero-copy，而不是「完全沒有成本」。它仍然有 cache、memory ordering、busy polling、CPU 排程等成本。

### 4.4 SHM ABI 現況：兩條 SHM path 不應混用資料 slot

`SHM syscall` 與 `SHM mmap` 都來自 `/dev/shm_ipc` 和同一份 `g_shm` backing pages，但目前 kernel 與 user 對 `data` 欄位的 offset 不一致：

| Layout | `head` | `tail` | metadata | `data` |
| --- | ---: | ---: | ---: | ---: |
| kernel `struct shm_region` | 0 | 64 | `capacity=128`, `msg_size=132` | 136 |
| user `shm_region_t` | 0 | 64 | `meta=128..191` | 192 |

因此文件不能說「SHM syscall 和 SHM mmap 完全使用同一個 shared ring layout」。比較精準的說法是：

- 兩條路徑共用同一個 device 與同一份 mapped backing pages。
- `head` / `tail` offset 相同，所以 ring index 可被雙方看到。
- `data` offset 不同，所以目前不應混用「syscall path 寫資料、mmap path 讀資料」或反過來。
- benchmark 的第 3 種測試由 user producer / user consumer 都依照 `user/common.h` 的 layout 操作，因此可獨立觀察 mmap path 成本。

---

## 5. 效能比較應該怎麼看

| 比較 | 可以觀察什麼 |
| --- | --- |
| MQ syscall vs SHM syscall | 同樣有兩次資料複製，但 queue 結構與同步方式不同。 |
| SHM syscall vs SHM mmap | 同樣經由 `/dev/shm_ipc`，但 SHM syscall 每筆進 kernel 並複製資料；SHM mmap 只在 setup 進 kernel，runtime 直接操作 mapped page。注意目前兩者 `data` offset 不一致，不應把資料 slot 混用。 |
| `/proc/mq_stats` vs `/proc/shm_stats` | kernel 模組內部統計是否符合 benchmark 執行狀態。 |

注意事項：

- benchmark 的 `msg/s` 是吞吐量（Throughput），不是單筆最小延遲。
- mmap path 每筆訊息不會呼叫 kernel `shm_write()` / `shm_read()`，因此 `/proc/shm_stats` 的 `write_count` / `read_count` 不會完整代表 mmap worker 的每筆 user-space 操作。
- 若機器上有其他高負載程式，忙碌輪詢會受到 CPU 排程影響。

---

## 6. API 選用理由摘要

更完整的 API 教學與比較在 [report_ipc_api.md](report_ipc_api.md)。這裡先整理主要取捨。

| 選用 | 類似選項 | 選用原因 |
| --- | --- | --- |
| `kfifo` | 手寫 linked list、手寫 ring | kernel 已提供 FIFO helper，適合固定大小訊息佇列示範。 |
| `wait_queue` | busy polling、completion | MQ 需要阻塞語意；沒有資料時讓 reader 睡眠，比一直輪詢合理。 |
| `spinlock` | `mutex` | SHM syscall critical section 很短，原本目標是降低 lock overhead。 |
| `vmalloc` | `kmalloc`、`alloc_pages` | ring buffer 大小固定但可能超過連續實體記憶體需求，`vmalloc` 容易取得連續虛擬位址。 |
| `remap_pfn_range` | `remap_vmalloc_range`、`vm_insert_page` | 本專案想展示 PFN 與 VMA 映射流程，因此逐頁轉 PFN 後 remap。 |
| `procfs` | `sysfs`、`debugfs` | 統計資料是文字型、多欄位輸出，`procfs` + `seq_file` 寫法直接。 |

---

## 7. 開發 BUG、原因與解法

這一節把開發過程中遇到或從目前程式碼可觀察到的問題整理成可追蹤的格式。不是每一項都代表目前已完全修完；若屬於目前仍存在的風險，會明確標示。

### 7.1 Linux kernel API 版本差異：`class_create()`

問題現象：

- 在較新的 kernel 上編譯 module 時，舊寫法可能出現 `class_create` 參數不符。

原因：

- Linux 6.4 之後，`class_create()` 改為單參數形式。
- 舊版常見寫法是 `class_create(THIS_MODULE, "name")`。
- 目前程式使用的是 `class_create("name")`。

解法：

```c
g_class = class_create(MQ_DEVICE "_class");
g_class = class_create(SHM_DEVICE "_class");
```

分析：

這是 kernel API 變動，不是邏輯錯誤。寫 kernel module 時不能只看舊教學，要對照目標 kernel 的 header。這也是專案文件要寫清楚 kernel 版本的原因。

### 7.2 Linux kernel API 版本差異：`vm_flags`

問題現象：

- 舊程式若直接寫 `vma->vm_flags |= ...`，在新版 kernel 可能編譯失敗。

原因：

- 新版 kernel 對 `vm_area_struct` 欄位存取逐漸改用 helper。
- 直接改欄位會破壞 kernel 對 VMA flags 的封裝。

解法：

```c
vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
```

分析：

`mmap()` 相關程式很容易遇到 kernel 版本差異。遇到這種錯誤時，不應該只用強制轉型或改 header 壓過去，而是要找新版 kernel 建議的 helper API。

### 7.3 False Sharing：`head` 與 `tail` 放太近

問題現象：

- mmap path 的 throughput 不穩定，或比預期低。
- producer 只改 `head`，consumer 只改 `tail`，理論上互不衝突，但效能仍受影響。

原因：

- CPU cache 以 cache line 為單位搬移，常見大小是 64 bytes。
- 如果 `head` 與 `tail` 在同一條 cache line，兩個 CPU 即使修改不同欄位，也會讓對方的 cache line 失效。
- 這就是 false sharing。

解法：

```c
volatile uint32_t head;
uint8_t pad1[60];

volatile uint32_t tail;
uint8_t pad2[60];
```

分析：

`head` 與 `tail` 的資料量很小，但更新頻率很高。這種欄位比大資料本身更容易造成 cache coherence 成本。padding 不是為了節省記憶體，而是為了讓高頻更新欄位不要互相干擾。

### 7.4 user/kernel shared layout 不同步的風險

問題現象：

- mmap demo 單獨跑可能正常。
- 但如果要讓 syscall path 寫入、mmap path 讀出，或反過來，可能讀到錯誤 slot。

原因：

- `kernel/shm_module.c` 定義 `struct shm_region`。
- `user/common.h` 定義 `shm_region_t`。
- 兩邊是手動維護 layout，沒有共用 header、magic number、version、offset 檢查。
- 目前 user header 把 `capacity/msg_size` 包在 64-byte `meta` 區塊中；kernel struct 則在 `msg_size` 後直接接 `data`。這會讓 `data` offset 有不一致風險。

解法方向：

1. 讓 kernel 與 user 使用同一份可共享的 layout 定義。
2. 至少加入 compile-time 或 runtime 檢查，例如檢查 `offsetof(data)`。
3. 若要保留 64-byte `meta` padding，kernel struct 也要同步加上相同 padding。
4. 若不需要 `meta` padding，user header 要移除，讓 `data` offset 與 kernel 一致。

分析：

`mmap()` 的困難點不只在映射頁面，還在 ABI。只要 user 與 kernel 對同一段 bytes 的解讀不同，程式可能不會立刻 crash，卻會讀寫到不同語意的位置。這種 bug 很適合用 layout diagram、`offsetof()` 與固定版本欄位預防。

### 7.5 `copy_from_user()` 放在 `spinlock` 區段內

狀態：目前程式碼仍可觀察到此風險。

問題現象：

- 在某些情況下，kernel 可能警告在不可睡眠情境呼叫可能睡眠的函式。
- 若 user pointer 對應頁面尚未進記憶體，copy 可能觸發 page fault。

原因：

- `spinlock` 保護區段不應執行可能睡眠的操作。
- `copy_from_user()` / `copy_to_user()` 雖然是 kernel 常用 API，但它們處理 user pointer 時可能遇到 page fault。
- `shm_write()` 目前在 `spin_lock(&g_spin)` 後呼叫 `copy_from_user()`。

建議解法：

```text
shm_write():
  copy_from_user(tmp, ubuf, len)   # 先在 lock 外複製
  spin_lock(&g_spin)
  檢查 ring 是否有空間
  memcpy(g_shm->data[head], tmp, MSG_SIZE)
  更新 head
  spin_unlock(&g_spin)
```

分析：

如果目標是安全性與可維護性，應避免 user copy 在 spinlock 內。若擔心多一次 `memcpy()` 影響比較結果，可以在文件中註明這條 syscall path 是教學對照用，並另外保留 mmap path 作為 zero-copy 主軸。

### 7.6 Short write 造成未定義資料被送入 slot

狀態：目前程式碼仍可觀察到此風險。

問題現象：

- caller 若呼叫 `write(fd, buf, len)` 且 `len < MSG_SIZE`，module 仍回傳 `MSG_SIZE`。
- `mq_write()` 的 stack buffer 或 SHM slot 剩餘 bytes 可能保留舊資料。

原因：

- `mq_write()` 與 `shm_write()` 只把 `len` bytes 從 user 複製進來。
- 但後續仍以固定 64 bytes message commit。

建議解法：

```c
if (len != MSG_SIZE)
    return -EINVAL;
```

或：

```c
memset(kb, 0, MSG_SIZE);
copy_from_user(kb, ubuf, len);
```

分析：

IPC protocol 若固定訊息大小，最簡單清楚的做法是要求 caller 一定寫滿 `MSG_SIZE`。若要支援變長訊息，應該另外存長度欄位，而不是把短資料塞進固定 slot。

### 7.7 `scripts/03_benchmark.sh` 依賴 `bc`

問題現象：

- 執行 benchmark script 時出現 `bc: command not found`。

原因：

- script 用 `bc` 計算資料量。
- 環境若沒有安裝 `bc`，script 會在計算階段失敗。

解法：

```bash
sudo apt install -y bc
```

或改寫為：

```bash
awk "BEGIN { printf \"%.1f\", ${COUNT} * 64 / 1048576 }"
```

分析：

這類問題不是 kernel bug，但會影響重現性。專題文件應該把工具依賴寫清楚，讓使用者不會把環境問題誤判為 IPC 實作問題。

### 7.8 `benchmark.c` 的 mmap failure 會回傳 0

狀態：目前程式碼仍可觀察到此風險。

問題現象：

- `mmap()` 失敗時會 `perror("mmap")`，但最後走到 `done` 並 `return 0`。
- shell script 可能把失敗當成成功。

原因：

- cleanup path 與 error path 共用 `done` label，但沒有保存錯誤狀態。

建議解法：

```c
int rc = 0;

if (shm == MAP_FAILED) {
    perror("mmap");
    rc = 1;
    goto done;
}

done:
    ...
    return rc;
```

分析：

benchmark 類程式的 exit code 很重要，因為它通常會被 script 或 CI 呼叫。錯誤訊息只給人看，exit code 才能讓自動化流程判斷成功或失敗。

---

## 8. 限制與可延伸方向

目前專案適合用來理解 IPC 成本，但不是完整通用 IPC framework。限制如下：

- mmap ring 假設單一 producer 與單一 consumer。多 producer 或多 consumer 需要額外同步設計。
- shared layout 需要更嚴格的 ABI 檢查。
- `/proc/*_stats` 是觀測用，統計值不等於完整 tracing。
- busy polling 會消耗 CPU，低延遲與 CPU 使用率需要取捨。
- 若要上 production，需要補上更完整的錯誤處理、權限控管與 lifetime 管理。

可延伸方向：

- 使用 C11 atomic 或 kernel atomic primitive 重寫 mmap ring。
- 加入 futex，讓 mmap path 在空或滿時能睡眠，而不是一直輪詢。
- 比較 `remap_vmalloc_range()` 與目前逐頁 `remap_pfn_range()` 寫法。
- 加入 `offsetof()` 檢查與 layout version，避免 user/kernel ABI mismatch。
- 設計 multi-producer / multi-consumer ring。

---

## 9. 小結

本專案的核心價值在於把 IPC 成本拆成可觀察的幾層：syscall、copy、queue/ring、同步、cache。MQ path 呈現傳統 syscall IPC 的成本；SHM syscall path 用來隔離 queue/ring 結構差異；SHM mmap path 則展示避開每筆 user/kernel copy 後，必須自行承擔同步與 ABI 管理責任。

共享記憶體不是「自動比較快」，而是把資料搬移責任從 kernel 轉到使用者空間。當資料路徑清楚、同步規則正確、layout 一致時，它可以減少每筆訊息的 syscall 與資料複製成本；如果同步或 layout 沒處理好，錯誤也會更難查。
