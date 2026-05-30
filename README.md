# Linux Kernel & Firmware Engineering Portfolio

本儲存庫收錄六個可獨立建置、獨立執行的子專題，涵蓋 Linux 字元驅動、平台驅動（Platform Driver）、行程間通訊（IPC）量測、POSIX Shell、CPU 排程模擬，以及 SSD 寫入路徑模擬器。各子目錄有自己的 `README_*.md` 與 `report_*.md`，本頁說明整包專案如何從零跑起來。

| 子專題 | 路徑 | 執行空間 | 一句話 |
|--------|------|----------|--------|
| SSD 寫入路徑模擬 | [`ssd-fw-sim/`](ssd-fw-sim/) | User | 模擬 NVMe SQ/CQ → FTL → NAND/GC |
| 字元裝置驅動 | [`chardev-driver/`](chardev-driver/) | Kernel + User | cdev + ioctl + proc/sysfs |
| IPC 效能對照 | [`linux-ipc-benchmark/`](linux-ipc-benchmark/) | Kernel + User | MQ（kfifo）vs SHM（mmap） |
| 韌體迷你 Shell | [`fwsh/`](fwsh/) | User | fork/pipe + readline + 內建 hexdump/crc32 |
| QEMU 平台驅動 | [`qemu-platform-demo/`](qemu-platform-demo/) | Kernel（ARM64） | DT + MMIO LED + sysfs |
| CPU 排程模擬 | [`cpu-scheduling-qemu/`](cpu-scheduling-qemu/) | User + QEMU 腳本 | FCFS/SJF/SRTF/Priority/RR |

**授權**：各 kernel module 原始碼標示 GPL-2.0；使用者空間工具請以各檔案標頭為準。

---

## YT DEMO:

https://youtu.be/477eGS1VDNE

---

## 建議環境

| 項目 | 說明 |
|------|------|
| 主機 | Ubuntu 22.04 / 24.04 x86_64（多數腳本在此驗證） |
| 權限 | 載入 kernel module、操作 `/dev/*` 需 **root**（`sudo`） |
| 網路 | `cpu-scheduling-qemu`、`qemu-platform-demo` 會下載映像檔或 kernel 原始碼 |

### 共用套件（可先裝一次）

在**儲存庫根目錄**開啟終端機，執行：

```bash
sudo apt update
sudo apt install -y build-essential gcc make \
    linux-headers-$(uname -r) kmod \
    libreadline-dev \
    qemu-system-x86 qemu-system-arm qemu-utils \
    device-tree-compiler bc bison flex libssl-dev libelf-dev wget
```

若要做 ARM64 平台 demo，還需要：

```bash
sudo apt install -y gcc-aarch64-linux-gnu
```

---

## 子專題一：`ssd-fw-sim`

模擬 host trace 驅動的寫入路徑：NVMe 提交/完成佇列、內部 request queue、FTL 映射、NAND 頁狀態、greedy GC、延遲統計。

### 建置

```bash
cd ssd-fw-sim
make
```

產物：`ssd_fw_sim`（主程式）、`ssd_fw_sim_tests`（`make test` 時編譯）。

### 執行

```bash
# 使用內建 sample trace
./ssd_fw_sim traces/sample.trace

# 指定設定檔與 CSV 輸出
./ssd_fw_sim --config ssd.conf --csv stats.csv traces/sample.trace
```

Trace 格式（僅解析 `WRITE` 行）：

```text
WRITE <lba> <size_in_pages>
```

### 驗證

```bash
make test
./ssd_fw_sim_tests
```

詳細 API 與呼叫鏈見 [`ssd-fw-sim/report_ssd_api.md`](ssd-fw-sim/report_ssd_api.md)。

---

## 子專題二：`chardev-driver`

字元裝置 `/dev/chardev0`：read/write/ioctl，以及 `/proc/chardev_info`、`/sys/class/chardev/chardev0/*`。

### 建置

**終端機 A**（專案目錄）：

```bash
cd chardev-driver/driver
make
```

成功後應有 `driver/chardev.ko`。

### 載入模組

仍在 `chardev-driver` 目錄：

```bash
chmod +x scripts/*.sh
sudo ./scripts/load.sh
```

腳本會對目前開機 kernel 的 `build` 目錄編譯、 `insmod`、並嘗試 `chmod 666 /dev/chardev0`。

### DEMO（建議兩個終端機）

**終端機 A** — 看 kernel log：

```bash
sudo dmesg -w | grep chardev
```

**終端機 B** — 操作裝置（需先 `cd` 到 `chardev-driver` 或知道裝置路徑）：

```bash
echo "Hello Driver" | sudo tee /dev/chardev0
cat /dev/chardev0
cat /proc/chardev_info
echo 1 | sudo tee /sys/class/chardev/chardev0/read_only
echo "blocked" | sudo tee /dev/chardev0    # 預期被拒絕
```

**完整 ioctl 測試**（終端機 B）：

```bash
cd chardev-driver/userspace
make
sudo ./test_app
```

### 卸載

```bash
cd chardev-driver
sudo ./scripts/unload.sh
```

---

## 子專題三：`linux-ipc-benchmark`

兩個模組：`mq_module.ko`（`/dev/mq_ipc`）、`shm_module.ko`（`/dev/shm_ipc`）。使用者程式 `benchmark` 對照三種路徑（MQ syscall、SHM syscall、SHM mmap）。

### 建置與載入

**必須 root** 執行 setup 腳本：

```bash
cd linux-ipc-benchmark
sudo bash scripts/01_setup.sh
```

腳本會：安裝依賴、編譯 kernel/user、 `insmod` 兩個模組、設定 `/dev/mq_ipc` 與 `/dev/shm_ipc` 權限。

### DEMO（建議三個終端機）

| 終端機 | 工作目錄 | 指令 |
|--------|----------|------|
| A | `linux-ipc-benchmark` | 已完成 setup，可保持 idle |
| B | 任意 | `watch -n 1 cat /proc/mq_stats` 或 `watch -n 1 cat /proc/shm_stats` |
| C | `linux-ipc-benchmark/user` | `./benchmark`（預設 200000 筆訊息） |

可選：先跑示範程式

```bash
cd linux-ipc-benchmark/user
./mq_demo
./shm_demo
```

（若腳本有提供 `scripts/02_demo.sh`，亦可 `sudo bash scripts/02_demo.sh`。）

### 清理

```bash
cd linux-ipc-benchmark
sudo bash scripts/04_cleanup.sh
```

---

## 子專題四：`fwsh`

自製 Shell：管線（`|`）、重導向、背景（`&`）、內建 `hexdump` / `crc32` / `memmap`。

### 建置

```bash
cd fwsh
make
```

依賴 `libreadline-dev`（見上方共用套件）。

### 執行

```bash
./fwsh
```

範例指令：

```bash
help
hexdump ./fwsh 0x80
crc32 ./fwsh
memmap
ls | wc -l
sleep 3 &
```

### 其他 make 目標

```bash
make clean
make debug      # AddressSanitizer 建置
make valgrind   # 需已安裝 valgrind
```

---

## 子專題五：`qemu-platform-demo`（耗時較長）

在 QEMU ARM64 `virt` 上交叉編譯 kernel 6.6.30、overlay DTB、編譯 `myled_ctrl.ko`、打包 initramfs 並啟動。

### 流程概覽

在 `qemu-platform-demo` 目錄**依序**執行（勿跳步）：

```bash
cd qemu-platform-demo
bash scripts/00_install_deps.sh    # 若存在；否則用上方 apt 套件
bash scripts/01_build_kernel.sh    # 約 10–20 分鐘，需下載 kernel
bash scripts/02_patch_dtb.sh
bash scripts/03_build_driver.sh
bash scripts/04_build_rootfs.sh    # 需本機有 BusyBox，路徑見腳本內 BUSYBOX 變數
bash scripts/05_run_qemu.sh
```

QEMU 內建 shell 出現後：

```bash
/test_myled.sh
```

手動 sysfs 範例（路徑以 `find /sys -name enable 2>/dev/null` 為準）：

```bash
cd /sys/bus/platform/devices/10010000.myled-controller/myled/
echo 1 > enable
echo 200 > brightness
cat info
```

離開 QEMU：按 `Ctrl-A` 再放開，接著按 `x`。

### 清理

```bash
bash scripts/06_clean.sh
```

---

## 子專題六：`cpu-scheduling-qemu`

**注意**：`src/scheduler.c` 是在**主機上編譯的 user-space 排程模擬器**，透過 QEMU VM 與 cloud-init 跑 benchmark；**並未修改 Linux kernel 的 CFS/排程器**。

### 建置模擬器（主機）

```bash
cd cpu-scheduling-qemu
make
# 或 gcc -O2 -o scheduler src/scheduler.c
```

手動餵 stdin 測試：

```bash
./scheduler fcfs < src/workload_demo.txt
```

### 完整 QEMU 流程（依序）

```bash
cd cpu-scheduling-qemu
chmod +x scripts/*.sh
bash scripts/01_setup_env.sh    # 下載 cloud image、編譯 scheduler、準備 vm/
bash scripts/02_start_vm.sh     # 背景 VM，SSH port 2222
bash scripts/03_demo.sh         # 結果：results/demo_output.txt
bash scripts/04_benchmark.sh    # 結果：results/benchmark_report.txt
bash scripts/05_cleanup.sh
```

VM 登入（選用）：`ssh -p 2222 scheduler@localhost`（腳本內示範密碼見 `01_setup_env.sh` 的 `VM_PASS`）。

---

## 專案亮點（實作面）

- **分層清楚**：驅動、IPC、Shell、模擬器各自獨立 Makefile，可單獨 clone 子目錄練習。
- **可觀測**：多處使用 `/proc`、`sysfs`、`dmesg`、統計輸出（如 `stats_print`、`/proc/mq_stats`）。
- **對照實驗**：IPC 三條路徑隔離 copy 與 syscall 成本；SSD 模擬器拆分 queue / service latency。
- **錯誤路徑**：如 `ssd_config_validate`、`nvme_submit_write` 滿佇列重試、chardev init 的 `goto err_*` 回滾。

---

## 技術文件索引

| 文件 | 說明 |
|------|------|
| [`report.md`](report.md) | 全專案技術報告（架構、呼叫鏈、限制） |
| [`QA.md`](QA.md) | 面試問答 |
| [`Interview.md`](Interview.md) | 深度程式碼分析 |
| 各子目錄 `report_*.md` | 子專題細部報告 |

---

## 常見問題

| 現象 | 可能原因 |
|------|----------|
| `make` 找不到 kernel headers | 安裝 `linux-headers-$(uname -r)`，kernel 模組需與**目前開機版本**一致 |
| `/dev/chardev0` 不存在 | `insmod` 失敗，查 `dmesg`；或 udev 尚未建立節點 |
| `insmod` 版本不符 | 用錯 headers，需重新 `make` 後再載入 |
| `benchmark` 打不開裝置 | 未跑 `01_setup.sh` 或 cleanup 後未重載模組 |
| QEMU 無法啟動 | KVM 權限、`BUSYBOX` 路徑、DTB overlay 是否成功 |

---

## 後續可擴充方向

1. **ssd-fw-sim**：接入 `free_block_pool_get_min_erase_block` 做 wear 考量；支援 READ trace；多執行緒 host。
2. **chardev**：`read_only` 與 buffer 同一 mutex；`poll`/`epoll` 支援。
3. **linux-ipc-benchmark**：mmap 路徑改 C11 atomic；多 producer/consumer 測試。
4. **fwsh**：job control（`fg`/`bg`）；環境變數展開。
5. **qemu-platform-demo**：DT 加入 interrupt；`led_classdev` 整合。
6. **cpu-scheduling-qemu**：納入 context switch 成本；多核心 ready queue 模型。

---

## 授權

Kernel module 相關原始碼依各檔案 SPDX 標示（多為 GPL-2.0）。使用前請遵守對應授權條款。
