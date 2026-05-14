# Linux Character Device Driver: Multi-Interface System Design

![Kernel Version](https://img.shields.io/badge/Kernel-5.15%2B-blue.svg)
![License](https://img.shields.io/badge/License-GPL--2.0-green.svg)
![Platform](https://img.shields.io/badge/Platform-Ubuntu%2022.04-orange.svg)

本專案實作了一個功能完備的 Linux 字元裝置驅動程式，不僅涵蓋了基礎的檔案 I/O 操作，更深度整合了核心通訊的三大支柱：**ioctl (帶外控制)**、**procfs (系統監控)** 與 **sysfs (屬性配置)**。

這不只是一個 HelloWorld 級別的驅動，它展示了如何在 Linux 核心中安全地管理緩衝區、處理多執行緒並行衝突，以及如何建立一套符合核心規範的監控介面。

---

## 💡 專案亮點與實作特色

- **多維度互動介面**：同一套驅動支援四種互動方式（VFS, ioctl, proc, sysfs），方便在不同情境下呼叫。
- **工業級安全性**：嚴格使用 `copy_from_user` 與 `copy_to_user` 進行邊界檢查，並利用 `mutex` 互斥鎖確保資料一致性。
- **自動化裝置管理**：整合 `udev` 機制，模組載入時會自動動態申請主設備號 (Major Number) 並產生 `/dev/chardev0`，無需手動 `mknod`。
- **無鎖統計計數**：使用核心原子變數 `atomic_t` 追蹤讀寫次數，兼顧效能與精準度。

---

## 🛠️ 開發環境與前置需求

- **作業系統**：建議使用 Ubuntu 22.04 LTS 或任何 Linux Kernel 5.15 以上版本。
- **必要工具**：需具備 `gcc`, `make` 以及對應核心版本的 `kernel-headers`。

安裝指令：
```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r)
```

---

## 🚀 建立與部署流程

### 1. 下載並進入目錄
```bash
# 請確保您位於專案的 chardev-driver 目錄下
cd chardev-driver
```

### 2. 編譯驅動模組
```bash
# 進入驅動原始碼目錄
cd driver
make
# 成功後會產生 chardev.ko 檔案
```

### 3. 載入驅動
我們建議使用專用的腳本來載入，因為它會自動處理設備權限：
```bash
# 回到 chardev-driver 根目錄
cd ..
chmod +x scripts/*.sh
sudo ./scripts/load.sh
```

---

## 🎬 完整驗證與 DEMO 步驟

為了觀察驅動程式在底層的運作情形，建議開啟**兩個終端機視窗**。

### 步驟 A：即時日誌追蹤 (Terminal 1)
開啟一個新的視窗，執行以下指令以持續監看核心日誌：
```bash
# 在 Terminal 1 執行
sudo dmesg -w | grep chardev
```

### 步驟 B：實機操作演示 (Terminal 2)
在原始視窗執行以下測試：

#### 1. 基礎寫入與讀取
```bash
# 寫入一段測試文字
echo "Hello Driver" > /dev/chardev0

# 讀取剛才寫入的內容
cat /dev/chardev0
```
*此時 Terminal 1 應會顯示 `write() 13 bytes` 與 `read() 13 bytes` 的紀錄。*

#### 2. 檢視 procfs 即時狀態
```bash
# 查看驅動內部目前的統計數據與緩衝區內容
cat /proc/chardev_info
```

#### 3. sysfs 動態切換唯讀模式
```bash
# 預設為讀寫模式，我們將其改為唯讀 (1)
echo 1 | sudo tee /sys/class/chardev/chardev0/read_only

# 再次嘗試寫入，此時應會回傳「拒絕存取」
echo "Try to hack" > /dev/chardev0
# 預期輸出：-bash: /dev/chardev0: Permission denied
```

#### 4. 執行 ioctl 完整測試程式
本專案提供了一個 C 語言撰寫的測試程式，專門測試 ioctl 控制碼：
```bash
cd userspace
make
sudo ./test_app
```

---

## 🧹 清理環境
當您完成測試後，請執行卸載腳本：
```bash
# 回到 chardev-driver 根目錄
sudo ./scripts/unload.sh
```

---

## 📌 未來可擴充方向

1. **支援 Poll/Select**：實作非阻塞式 I/O，讓應用程式能使用 `epoll` 監控驅動狀態。
2. **中斷處理 (Interrupt Handling)**：模擬硬體觸發中斷，並實作 Tasklet 或 Workqueue 來處理後半部 (Bottom Half) 邏輯。
3. **支援 DMA 傳輸**：針對大數據傳輸場景，實作直接記憶體存取機制，降低 CPU 負荷。
4. **多設備支援**：修改驅動使其支援多個 Minor Number，能同時管理多個虛擬緩衝區。
