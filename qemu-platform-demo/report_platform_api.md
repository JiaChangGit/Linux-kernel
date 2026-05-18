# QEMU Platform Device Driver API 技術報告

本報告針對 `/qemu-platform-demo` 子專案進行深度 Codebase Trace 與架構分析。內容完全基於實體原始碼 (`myled_ctrl.c`)、Device Tree 片段 (`myled-fragment.dts`) 及自動化腳本的實際實作。

---

## 第一階段：Codebase Trace (程式碼追蹤)

### 1. Project Structure (專案結構)

- **Source Files**:
    - `driver/myled_ctrl.c`: 核心 Platform Driver 實作，包含 DT 解析、MMIO 存取與 sysfs 介面。
- **Header Files**:
    - `driver/myled_ctrl.h`: 定義暫存器偏移量、位元欄位及驅動私有資料結構 `myled_priv`。
- **Hardware Description**:
    - `dts/myled-fragment.dts`: 定義虛擬 LED 控制器的 Device Tree 節點，包含基底位址 (0x10010000) 與自定義屬性。
- **Build & Run System**:
    - `scripts/01_build_kernel.sh`: 建置 ARM64 核心。
    - `scripts/02_patch_dtb.sh`: 使用 `fdtoverlay` 將 DTS 片段注入 QEMU 產生的 DTB。
    - `scripts/03_build_driver.sh`: 編譯驅動模組。
    - `scripts/05_run_qemu.sh`: 啟動模擬環境。

### 2. Semantic Element Extraction (語義要素萃取)

- **API (Platform/OF)**: `module_platform_driver`, `platform_get_resource`, `devm_ioremap_resource`, `of_property_read_u32`, `of_property_read_string`。
- **Sysfs Interface**: `DEVICE_ATTR_RW`, `DEVICE_ATTR_RO`, `sysfs_create_group`, `sysfs_emit`。
- **Synchronization**: `spinlock_t` (保護暫存器讀寫路徑)。
- **Memory Management**: 使用 `devm_kzalloc` 進行具備生命週期管理的記憶體配置。
- **Execution Model**: **Platform Bus 驅動模型**。透過 Device Tree 的 `compatible` 字串進行匹配與探測 (Probing)。
- **Hardware Abstraction**: 實作了 **Simulated Mode**，當 MMIO 資源不可得或讀取異常時，自動切換至內部 Shadow Register 陣列。

### 3. API / Macro Inventory (依照功能分類)

| 分類 | 元素名稱 | 類型 | 呼叫位置 | 用途 |
| :--- | :--- | :--- | :--- | :--- |
| **Registration** | `module_platform_driver` | macro | `myled_ctrl.c:385` | 註冊 Platform Driver 進入點與結束點。 |
| **Matching** | `myled_of_match` | struct | `myled_ctrl.c:367` | 定義相容字串 `myvendor,myled-v1` 用於 DT 匹配。 |
| **Probe Path** | `myled_probe` | callback | `myled_ctrl.c:268` | 裝置匹配成功後的初始化，包含資源獲取與 HW 啟動。 |
| **IO Path** | `myled_reg_read/write` | function | `myled_ctrl.c:38` | 封裝 `readl/writel`，並實作 Simulated Mode 切換。 |
| **Power Mgmt** | `myled_suspend/resume` | callback | `myled_ctrl.c:345` | 處理系統休眠與喚醒時的控制器狀態管理。 |

### 4. Call Graph (呼叫圖譜)

- **Initialization Chain**:
    `module_platform_driver` -> `platform_driver_register` -> `platform_bus_match` (DT compatible) -> `myled_probe`
    -> `devm_kzalloc` (配置私有結構)
    -> `of_property_read_*` (解析 DT 屬性)
    -> `platform_get_resource` & `devm_ioremap_resource` (映射 MMIO)
    -> `myled_hw_init` (硬體驗證與初值設定)
    -> `sysfs_create_group` (建立使用者介面)

- **Runtime Path (User Interaction)**:
    `echo 255 > brightness` -> `brightness_store` -> `kstrtou32` -> `myled_reg_write` -> `writel` (或寫入 shadow 陣列)

- **Power Management Chain**:
    `system suspend` -> `myled_suspend` -> `myled_reg_clr_bits` (關閉控制位元以省電)

### 5. Struct / Resource Tracing (資源追蹤)

- **`struct myled_priv`**:
    - **定義**: `myled_ctrl.h:35`
    - **作用**: 驅動運行的 Context 物件。
    - **Allocation**: 使用 `devm_kzalloc` 分配，其生命週期與 `struct device` 綁定，當驅動移除或 Probe 失敗時自動釋放。
    - **Storage**: 透過 `platform_set_drvdata` 儲存在裝置結構中，供 sysfs 或 PM 回呼函式檢索。

- **MMIO Resource**:
    - **來源**: `myled-fragment.dts` 中的 `reg = <0x0 0x10010000 0x0 0x1000>`。
    - **Mapping**: `devm_ioremap_resource` 將實體位址 0x10010000 映射至核心虛擬位址 `priv->base`。

### 6. Execution Trace (執行流程)

```text
[Device Discovery]
Bootloader (scripts) patches DTB -> Kernel starts -> Platform Bus scans DT nodes
-> Matches "myvendor,myled-v1" node -> Invokes myled_probe

[Probe Execution]
1. Allocate priv struct
2. Read "num-leds" (4) and "default-brightness" (180) from DT
3. Map MMIO 0x10010000
4. Check HW version (0xAB01)
   - If match: hardware mode
   - If mismatch/failed: fallback to simulated mode
5. Register sysfs group 'myled'
6. Enable PM runtime

[System Suspend]
PM core -> myled_suspend -> Clear MYLED_CTRL_ENABLE bit
```

---

## 第二階段：Architecture / API Technical Report

### 1. 執行語義與匹配機制 (Execution Semantics)

本專案實作了一個典型的 **Linux Platform Device/Driver 模型**，其架構核心在於「硬體描述與驅動邏輯的分離」：

- **OF Matching**: 驅動程式不主動偵測硬體位址，而是宣告支援的 `compatible` 表。核心在解析 Device Tree 時發現匹配節點，才觸發 `probe`。這使得驅動能跨平台使用，只需修改 DTS 即可改變基底位址或參數。
- **Simulated Mode 韌性設計**：`myled_hw_init` 展現了韌體除錯常見的技巧。透過讀取 `VERSION` 暫存器驗證 MMIO 是否真正可達（返回 `0xffffffff` 常代表地址解碼錯誤）。此自動 Fallback 機制允許驅動在無硬體模擬的 QEMU 環境下仍能透過 sysfs 介面驗證軟體流程。

### 2. 資源管理與生命週期 (Lifecycle Management)

- **Managed Device API (`devm_*`)**: 專案大量使用 `devm_kzalloc` 與 `devm_ioremap_resource`。
    - **優勢**：開發者無需在 `remove` 路徑或錯誤回退路徑中顯式呼叫 `kfree` 或 `iounmap`。核心會追蹤這些資源的 Ownership，確保在裝置卸載時依序清理，降低了記憶體洩漏與懸空指標的風險。
- **Sysfs 封裝**：透過 `attribute_group` 將多個屬性（enable, brightness 等）打包。這種做法比分散呼叫 `device_create_file` 更具原子性，且在目錄結構中更為整潔。

### 3. 同步行為與 IRQ 安全 (Synchronization)

- **Spinlock 語義**：暫存器存取封裝在 `spin_lock_irqsave` 臨界區內。
    - **分析**：由於 Platform 暫存器讀寫通常極快，且不允許在 IRQ 上下文中進入睡眠，`spinlock` 是最適配的同步原語。使用 `irqsave` 版本確保了即使在自旋期間發生硬體中斷，驅動狀態也不會被破壞。

### 4. 數據流與 Device Tree 屬性解析

驅動透過 `of_property_read_u32` 等 API 實現了**「資料驅動 (Data-driven)」**的配置：
- **`num-leds`**: 決定內部邏輯處理的 LED 數量。
- **`label`**: 提供裝置的人類可讀標籤。
- **`default-brightness`**: 在 Probe 階段直接寫入暫存器，實現「開機即生效 (Init-on-boot)」的行為，這對於 LED 等狀態裝置非常重要。

### 5. 比較與分析 (類似機制比較)

- **`platform_device` vs `i2c_client`**: 雖然兩者都使用 Probe 機制，但 Platform 裝置通常是硬編碼（或 DT 定義）的 MMIO 裝置，不具備 I2C/SPI 等匯流排的動態列舉特性。
- **`sysfs_emit` vs `sprintf`**: 專案正確選用 `sysfs_emit`。相較於傳統 `sprintf`，它能感知 sysfs buffer 的邊界 (PAGE_SIZE)，防止緩衝區溢位。

### 6. 潛在風險與優化路徑 (Potential Bug/Risk)

- **Address Overlap**: Device Tree 中指定的 `reg` 區間 (0x1000) 若與系統其他重要裝置衝突，可能導致系統崩潰。專案透過 `devm_ioremap_resource` 進行了 `request_mem_region` 檢查，有效預防了此問題。
- **Simulated Mode 同步**: 在 Simulated Mode 下，`sim_regs` 陣列同樣受 `priv->lock` 保護，這保證了 sysfs 並發存取時的資料一致性。
- **PM Runtime**: 雖然呼叫了 `pm_runtime_enable`，但程式碼中未實作 `runtime_suspend/resume` 回呼。目前的省電邏輯主要依賴系統層級的 Suspend。

---
**結論**：`/qemu-platform-demo` 展示了一個現代 Linux 核心驅動的典範實作。透過整合 Device Tree 解析、Managed 資源管理與自動化 Fallback 機制，該驅動不僅具備高度的硬體相容性，也展現了優異的軟體魯棒性，是學習 Linux 平台驅動與虛擬硬體互動的理想案例。

