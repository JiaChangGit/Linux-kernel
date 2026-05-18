# Character Device Driver 深度架構與 API 技術報告

本報告針對 `/chardev-driver` 子專案進行嚴謹的 Codebase Trace 與架構分析。所有結論均基於實體程式碼的靜態分析與呼叫鏈追蹤。

---

## 第一階段：Codebase Trace (程式碼追蹤)

### 1. Project Structure (專案結構)

- **Source Files**:
    - `driver/chardev.c`: 驅動核心實作，包含 VFS、procfs 與 sysfs 邏輯。
    - `userspace/test_app.c`: 使用者空間測試工具，驗證驅動介面。
- **Header Files**:
    - `driver/chardev.h`: 定義 IOCTL Magic Number 及 Command 序號。
- **Build System**:
    - `Makefile`。
- **Scripts**:
    - `scripts/load.sh`: 封裝編譯、載入、權限設定與驗證流程。
    - `scripts/unload.sh`: 執行移除模組與清理驗證。
- **Relationship**: `test_app.c` 透過標準 `fcntl.h` 與 `sys/ioctl.h` 介面，配合 `chardev.h` 定義的協議與核心驅動溝通。

### 2. Semantic Element Extraction (語義要素萃取)

- **API (Kernel-side)**: `alloc_chrdev_region`, `cdev_init`, `cdev_add`, `class_create`, `device_create`, `proc_create`, `kzalloc`, `copy_to_user`, `copy_from_user`, `sysfs_emit`, `seq_printf`, `single_open`。
- **Macros**: `_IO`, `_IOR`, `_IOW` (IOCTL 定義); `DEVICE_ATTR_RO`, `DEVICE_ATTR_RW`, `ATTRIBUTE_GROUPS` (sysfs 屬性); `module_init`, `module_exit` (進入點)。
- **Callbacks (Operation Tables)**:
    - `struct file_operations chardev_fops`: 處理 VFS 系統呼叫。
    - `struct proc_ops chardev_proc_ops`: 處理 `/proc` 檔案存取。
- **Function Pointers**: `chardev_open`, `chardev_read`, `proc_show` 等掛載於上述 Operation Tables。
- **Synchronization Primitives**: `struct mutex lock`, `atomic_t open_count/read_count/write_count`。
- **Memory Management**: 使用 `kzalloc` 配置固定大小 (4096 bytes) 的核心緩衝區。
- **Registration Mechanism**: 動態申請 Major Number 並註冊為 Character Device。

### 3. API / Macro Inventory

| 名稱 | 類型 | 呼叫位置 | 用途 | 影響 |
| :--- | :--- | :--- | :--- | :--- |
| `module_init` | Annotation | `chardev.c:278` | 指定 `chardev_init` 為模組進入點。 | 決定模組載入時的初始化順序。 |
| `copy_from_user` | Function | `chardev.c:104` | 將使用者空間資料複製到核心 `drv.buf`。 | 核心資料寫入路徑的核心，受 `mutex` 保護。 |
| `copy_to_user` | Function | `chardev.c:82` | 將核心 `drv.buf` 資料傳回使用者空間。 | 核心資料讀取路徑的核心，配合 `*ppos` 處理。 |
| `DEVICE_ATTR_RW` | Macro | `chardev.c:197` | 生成 `dev_attr_read_only` 結構與 show/store 映射。 | 建立 sysfs 與內部變數 `drv.read_only` 的連結。 |
| `single_open` | Function | `chardev.c:166` | 初始化 `seq_file` 並綁定 `proc_show`。 | 簡化 procfs 讀取邏輯，自動處理讀取狀態。 |

### 4. Call Graph (呼叫圖譜)

- **Initialization Chain**:
    `module_init` -> `chardev_init`
    -> `kzalloc` (記憶體)
    -> `alloc_chrdev_region` (編號)
    -> `cdev_init` & `cdev_add` (VFS 掛接)
    -> `class_create` & `device_create` (sysfs/dev 節點)
    -> `proc_create` (procfs 掛接)

- **Runtime Data Path (Indirect Call via VFS)**:
    `userspace:read()` -> `vfs_read` -> `chardev_fops.read` (`chardev_read`)
    `userspace:write()` -> `vfs_write` -> `chardev_fops.write` (`chardev_write`)
    `userspace:ioctl()` -> `vfs_ioctl` -> `chardev_fops.unlocked_ioctl` (`chardev_ioctl`)

- **Procfs Path (Indirect Call via proc_ops)**:
    `cat /proc/chardev_info` -> `proc_open` -> `single_open` -> `proc_show` -> `seq_printf`

### 5. Struct / Resource Tracing

- **`static struct {...} drv`**:
    - **定義**: `chardev.c:37` (全域單體)。
    - **Allocation**: 靜態配置結構本體，內部 `buf` 由 `kzalloc` 動態配置。
    - **Ownership**: 由驅動模組完全擁有，不與其他模組共享。
    - **Lifetime**: 從 `chardev_init` 成功直到 `chardev_exit` 被執行。
    - **Release**: 於 `chardev_exit` 中呼叫 `kfree(drv.buf)`。

- **`dev_t devno`**:
    - **Allocation**: `alloc_chrdev_region` 動態取得。
    - **State**: 代表裝置在系統中的身分證 (Major/Minor)。
    - **Release**: `unregister_chrdev_region` 歸還給 kernel。

### 6. Execution Trace (執行追蹤)

```text
[Initialization Flow]
1. Allocate Buffer (kzalloc)
2. Init Locks (mutex_init, atomic_set)
3. Register Region (alloc_chrdev_region)
4. Init cdev (cdev_init + cdev_add)
5. Create Class & Device (class_create + device_create) -> Trigger udev to create /dev/chardev0
6. Create Proc Entry (proc_create)

[Write Data Flow]
Userspace Buf -> copy_from_user -> drv.buf (Protected by mutex)
                |
                v
        Check drv.read_only (Gatekeeper)
```

---

## 第二階段：Architecture / API Technical Report

### 1. Execution Semantics & Resource Management

本驅動採用**「全域狀態單體 (Global State Singleton)」**模式，將所有資源管理 (Memory, Locks, Device Handles) 封裝在 `static struct drv` 中。

- **Memory Flow**: 採用「預先配置 (Pre-allocation)」策略。`drv.buf` 在 `init` 時即固定配置 4096 bytes。`write` 操作會直接覆蓋該空間，而非動態增減，這避免了頻繁配置記憶體帶來的 Fragment 或 OOM 風險，但限制了資料彈性。
- **Ownership**: `drv.buf` 的 Ownership 始終鎖定在驅動層。使用者空間僅能透過 `copy_to_user` / `copy_from_user` 存取複本。

### 2. Callback Chain & Indirect Dispatch

驅動的核心在於將 VFS 呼叫轉發至內部實作：

- **VFS Dispatch**: `chardev_fops` 扮演了協議適配器的角色。`unlocked_ioctl` 尤其重要，它將通用的 `ioctl` 轉發至 `chardev_ioctl`，並在此處進行 Magic Number 驗證，建立了專屬的控制路徑。
- **Procfs Sequential logic**: 使用 `seq_file` 機制。這是一個間接呼叫鏈：`proc_open` 觸發 `single_open`，後者註冊了核心的 `proc_show`。當使用者讀取 `/proc/chardev_info` 時，Kernel 會反覆呼叫 `proc_show` 直到資料結束。

### 3. Synchronization Behavior (併發行為分析)

專案混合使用了兩種同步機制：
- **Atomic Operations (`atomic_t`)**: 用於 `open_count`, `read_count` 等計數器。這在高度併發存取時比 `mutex` 更有效率，因為它避免了 Thread 睡眠。
- **Mutex (`struct mutex`)**: 用於保護資料一致性 (Data Integrity)。在 `read`, `write`, `ioctl` (Reset) 中均有使用。
    - **臨界區範圍**: 包含 `copy_from_user` 過程。這意味著如果使用者空間的 Page Fault 發生在核心複製期間，驅動會持有鎖進入睡眠。

### 4. Lifecycle & State Transition

- **State: Initializing**: 資源逐一配置。若任一環節失敗，會依賴 `goto` 標籤觸發反向清理 (Cleanup Path)。
- **State: Operational**: 裝置節點可見，接收 VFS 命令。
- **State: Read-Only (Internal)**: 透過 `drv.read_only` 切換。此狀態不影響 VFS 連結，但會導致 `chardev_write` 直接回傳 `-EACCES`。這是一種**「應用層邏輯門鎖」**。
- **State: Terminating**: 註銷順序與初始化相反，確保無 VFS 引用後才釋放記憶體。

### 5. API 比較與分析

- **`copy_to_user` vs `memcpy`**: 本專案正確使用 `copy_to_user`。若誤用 `memcpy` 將無法偵測無效的使用者位址，可能導致 Kernel Panic。
- **`mutex_lock` vs `spin_lock`**: 專案選用 `mutex` 是因為資料交換 (`copy_to_user`) 可能導致睡眠，這在 `spin_lock` 中是嚴格禁止的。
- **`sysfs_emit` vs `sprintf`**: 在 sysfs show 函式中使用 `sysfs_emit` 是 Linux 5.10+ 的推薦做法，它能自動處理 Buffer 邊界與偏移，比舊有的 `sprintf` 更安全。

### 6. Potential Risks (潛在風險)

- **缺失 `lseek` 實作**: 雖然 `test_app.c` 呼叫了 `lseek(fd, 0, SEEK_SET)`，但 `chardev_fops` 並未定義 `.llseek`。這導致 VFS 使用預設行為，對於某些特殊裝置可能會產生非預期位址存取。
- **Write Buffer 覆寫**: 目前 `write` 實作會重置 `drv.buf_len` 並從頭寫入。若多個行程併發寫入，雖然有 `mutex` 保護，但會發生 Data Race (後者覆蓋前者)，而非 Append。

---
**結論**: 本驅動展示了典型的 Linux 裝置驅動架構，整合了多種核心介面。資源生命週期管理嚴謹，但在 VFS 完整性 (lseek) 與併發資料模型 (Append vs Overwrite) 上有優化空間。

