# QEMU ARM64 平台驅動開發：裝置樹、MMIO 與資源管理技術報告

本報告旨在深入探討 `qemu-platform-demo` 專案的技術細節。本專案模擬了一個現代嵌入式 Linux 設備的完整開發週期，重點展示了 **Platform Bus** 匹配機制、**裝置樹 (Device Tree)** 的動態處理、以及 **MMIO (Memory-Mapped I/O)** 暫存器的存取技術。

---

## 一、 技術核心與核心概念

本專案運用了多項 Linux 核心開發的關鍵技術：

-   **Platform Driver 框架**：處理非熱插拔（Non-discoverable）硬體的標準方式，透過名稱或裝置樹節點進行匹配。
-   **裝置樹 (OF, Open Firmware)**：將硬體描述從核心原始碼中分離，使單一核心映像檔能支援多種硬體平台。
-   **devm 資源管理**：利用「託管資源 (Managed Resources)」機制，自動處理記憶體申請與釋放，降低核心記憶體外洩 (Memory Leak) 風險。
-   **MMIO 暫存器操作**：透過 `readl` / `writel` 函式與記憶體映射的硬體暫存器進行通訊。
-   **sysfs 物件模型**：利用 `kobject` 屬性組將硬體控制邏輯暴露給使用者空間。

---

## 二、 專案架構與模組分工

專案由四個層次緊密結合而成：

1.  **硬體定義層 (DTS)**：`myled-fragment.dts` 定義了虛擬硬體的基地位址 (0x0d000000) 與相容性字串 (`myvendor,myled-v1`)。
2.  **核心驅動層 (Driver)**：`myled_ctrl.c` 實作了平台驅動邏輯。
3.  **系統模擬層 (QEMU)**：使用 ARM64 Virt 機器模擬處理器環境，並透過腳本將 DTS 注入執行時期。
4.  **驗證層 (Rootfs)**：基於 BusyBox 的 Initramfs，包含自動化測試腳本。

---

## 三、 程式碼追蹤與執行流程分析

以下依照系統執行的時序，追蹤主要函式的呼叫鏈與資料流：

### 1. 初始化與匹配流程 (Matching Flow)
當 QEMU 載入 DTB 並啟動核心後：
-   核心解析裝置樹，發現節點 `myled-controller@0d000000` 帶有 `compatible = "myvendor,myled-v1"`。
-   驅動程式載入時呼叫 `module_platform_driver(myled_driver)`，向系統註冊 `platform_driver` 結構。
-   **匹配機制**：Platform Bus 掃描裝置樹節點，發現與 `myled_of_match` 表中的相容性字串匹配。
-   **觸發 Probe**：系統自動呼叫 `myled_probe(struct platform_device *pdev)`。

### 2. 裝置建立流程 (Device Creation Flow)
進入 `myled_probe` 後的具體動作：
-   **私有資料配置**：呼叫 `devm_kzalloc` 分配 `struct myled_priv`，並將其連結至裝置。
-   **資源取得**：呼叫 `platform_get_resource` 取得 DTS 中定義的 `reg` (MMIO 位址區間)。
-   **位址映射**：呼叫 `devm_ioremap_resource` 將實體位址 0x0d000000 映射為核心虛擬位址 `priv->base`。
-   **硬體初始化**：呼叫 `myled_hw_init`。此函式會讀取版本暫存器，若回傳 `0xFFFFFFFF` (代表無硬體回應)，則將 `priv->simulated` 設為 true，啟動模擬模式。
-   **介面建立**：呼叫 `sysfs_create_group` 在 `/sys/bus/platform/devices/.../myled/` 下建立屬性節點。

### 3. 事件觸發與資料傳遞流程 (Event & Data Flow)
當使用者執行 `echo 1 > enable` 時：
-   **VFS 轉發**：核心接收到寫入請求，定位到 `enable_store` 回標函式。
-   **資料解析**：呼叫 `kstrtobool` 將字串轉換為布林值。
-   **暫存器操作**：呼叫 `myled_reg_set_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_ENABLE)`。
-   **硬體寫入**：內部判斷 `simulated` 狀態。若有實體硬體，則呼叫 `writel` 更新位址空間；否則更新 `priv->sim_regs` 陣列。

---

## 四、 關鍵函式與實作細節解析

### 1. `myled_probe` vs `myled_remove`
-   **`myled_probe`**：資源申請的起點。重點在於使用 `devm_` 前綴 API，這確保了即便 `probe` 在中途失敗，已申請的資源也會依序自動回退，不需手動寫大量的 `goto` 清理。
-   **`myled_remove`**：資源清理。只需處理非 `devm` 託管的資源（如 `pm_runtime_disable` 與手動建立的 `sysfs_remove_group`）。

### 2. MMIO 存取：`readl` / `writel`
這對函式包含了必要的記憶體屏障 (Memory Barriers)，確保對 I/O 暫存器的讀寫不會被 CPU 亂序執行優化。

### 3. 橫向對比：`kzalloc` vs `devm_kzalloc`
| 特性 | `kzalloc` | `devm_kzalloc` |
| :--- | :--- | :--- |
| **釋放時機** | 必須顯式呼叫 `kfree` | 驅動移除或 `probe` 失敗時自動釋放 |
| **安全性** | 容易忘記釋放導致 Memory Leak | 安全，生命週期與 `device` 綁定 |
| **適用對象** | 通用核心邏輯 | 平台驅動、設備專用私有資料 |

---

## 五、 次要函式說明與角色

-   **`myled_reg_read/write`**：底層封裝層。負責判斷硬體是否存在並執行實際的 `readl/writel` 或 `sim_regs` 存取。它扮演了硬體抽象層 (HAL) 的角色。
-   **`myled_suspend/resume`**：電源管理回標。展示了在系統休眠時如何關閉控制器以節省電力。
-   **`of_property_read_u32`**：裝置樹解析工具。用於讀取自定義屬性（如 `num-leds`），實現驅動的參數化配置。

---

## 六、 開發挑戰與除錯紀錄

1.  **位址衝突問題**：
    在選擇虛擬 LED 的 MMIO 位址時，曾因與 QEMU 的 PL011 UART 區段衝突導致核心崩潰。透過檢查 `/proc/iomem` 與 QEMU 原始碼，最終選定 `0x0d000000` 作為安全位址。
2.  **BusyBox 架構匹配**：
    由於核心為 64 位元，Initramfs 中的 BusyBox 必須是 ARM64 版本。專案中提供了 `scripts/0A_fix_busybox_arch.sh` 輔助處理環境差異。
3.  **DTS 符號缺失**：
    `fdtoverlay` 要求基礎 DTB 必須包含符號。我們在 `dts/patch_dtb.sh` 中透過 `qemu -machine dumpdtb` 並加上特定的編譯選項來解決此問題。

---

## 七、 結論與技術延伸

`qemu-platform-demo` 完整展現了嵌入式工程師在硬體點亮 (Bring-up) 階段的工作流程。

**後續探討議題：**
-   **DMA (Direct Memory Access)**：如何實作大數據塊的緩衝區傳輸。
-   **Device Tree Overlay**：研究如何在系統執行時期，透過 `configfs` 動態加載裝置樹片段。
-   **GPIO 子系統整合**：將虛擬暫存器映射至核心標準的 GPIO 框架，支援更通用的 `gpiod` API。
