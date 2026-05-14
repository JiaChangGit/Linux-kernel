# QEMU ARM64 Platform Driver Demo: Virtual LED Controller

[![Kernel Version](https://img.shields.io/badge/Kernel-6.6.30-orange.svg)](https://kernel.org)
[![Platform](https://img.shields.io/badge/Platform-QEMU--ARM64-blue.svg)](https://www.qemu.org/)
[![License](https://img.shields.io/badge/License-GPL--2.0-green.svg)](LICENSE)

本專案是一個完整的 **Linux 平台驅動程式 (Platform Driver)** 開發與驗證實驗室。我們從零建構了一個基於 ARM64 架構的虛擬系統，展示了現代嵌入式 Linux 開發的核心流程：從下載與交叉編譯核心、修補裝置樹 (Device Tree)、編譯外掛模組 (Out-of-tree Module) 到建構自定義的根檔案系統 (Rootfs)。

核心組件是一個名為 `myled_ctrl` 的虛擬 LED 控制器驅動。它不僅展示了 MMIO 暫存器的操作，還整合了 **裝置樹匹配 (OF Match)** 與 **sysfs 互動介面**，並具備自動硬體偵測功能，能在無實體硬體環境下自動切換至模擬模式。

---

## 💡 專案特色

- **全自動化工具鏈**：一鍵下載並交叉編譯 Linux 6.6.30 核心，適配 ARM64 架構。
- **動態裝置樹注入 (DTS Overlay)**：利用 `fdtoverlay` 將虛擬硬體定義 (0x10010000 區段) 直接注入 QEMU 的執行時期位址空間。
- **完善的驅動生命週期**：完整實作 `probe`、`remove`、`suspend` 與 `resume` 回標函式 (Callbacks)。
- **精簡的 Initramfs 佈署**：使用 BusyBox 構建極小化的啟動環境，確保實驗環境的純淨度。

---

## 📂 專案結構與模組分工

```text
qemu-platform-demo/
├── driver/            # 平台驅動原始碼 (核心邏輯、sysfs 實作)
├── dts/               # 裝置樹原始碼 (myled-fragment.dts) 與修補工具
├── rootfs/            # Rootfs 疊加層 (init 腳本、測試工具)
├── scripts/           # 五大階段自動化腳本 (01-05)
└── docs/              # 展示截圖
```

---

## 🛠️ 開發環境與前置需求

本專案建議在 **Ubuntu 22.04 / 24.04 (x86_64)** 環境下執行。請先安裝必要的交叉編譯與模擬工具：

```bash
sudo apt update
sudo apt install -y build-essential gcc-aarch64-linux-gnu \
                    qemu-system-arm device-tree-compiler \
                    bc bison flex libssl-dev libelf-dev wget
```

---

## 📦 完整建置流程 (Step-by-Step)

請嚴格按照編號順序執行腳本。

### 階段 1：核心建構
下載 Linux 6.6.30 原始碼並針對 ARM64 `defconfig` 進行交叉編譯。
```bash
# 確保位於 qemu-platform-demo 目錄
bash scripts/01_build_kernel.sh
```
*註：此步驟視 CPU 效能約需 10-20 分鐘。*

### 階段 2：裝置樹修補 (Patching)
導出 QEMU Virt 機器的基底 DTB，並將我們的虛擬 LED 節點注入其中。
```bash
bash scripts/02_patch_dtb.sh
```

### 階段 3：編譯平台驅動
使用階段 1 產出的核心標頭檔 (Headers) 編譯 `myled_ctrl.ko` 核心模組。
```bash
bash scripts/03_build_driver.sh
```

### 階段 4：封裝 Rootfs
將 BusyBox 二進位檔、自定義 `init` 行程、測試腳本與驅動程式封裝成 `initramfs.cpio.gz`。
```bash
bash scripts/04_build_rootfs.sh
```
*重要：若您的環境中 BusyBox 位址不同，請修改腳本內的 `BUSYBOX` 變數路徑。*

---

## 🎬 實機操作演示 (DEMO)

### 步驟 1：啟動 QEMU 模擬器
```bash
bash scripts/05_run_qemu.sh
```
啟動後，您會看到核心啟動日誌，最後進入 BusyBox Shell。
*註：若要退出 QEMU，請按下 `Ctrl-A` 後接著按 `X`。*

### 步驟 2：執行自動化驗證
進入系統後，執行預載的驗證腳本：
```bash
# 在 QEMU 終端機執行
/test_myled.sh
```
此腳本會自動載入模組，並測試所有的 sysfs 節點。

### 步驟 3：手動互動測試
您也可以手動操作 sysfs 來控制虛擬 LED：
```bash
# 切換到裝置目錄 (位址會因 DT 定義而定)
cd /sys/bus/platform/devices/10010000.myled-controller/myled/

# 開啟 LED
echo 1 > enable

# 設定亮度 (0-255)
echo 200 > brightness

# 設定顏色為青色 (Cyan, #00FFFF)
echo 00ffff > color

# 檢查目前硬體狀態
cat info
```

---

## 📌 未來探討與擴充方向

1.  **中斷機制 (Interrupts) 整合**：在 DTS 中加入 `interrupts` 屬性，實作按鈕觸發中斷並連動 LED 狀態的邏輯。
2.  **電源管理 (PM) 深度整合**：實作 `pm_runtime` 策略，觀察在裝置閒置時如何自動關閉控制器的「模擬供電」。
3.  **多實例並行**：在 DTS 中定義多個 `myled-controller` 節點（不同基地位址），驗證驅動程式的重入性 (Reentrancy) 與私有資料 (`priv`) 的隔離。
