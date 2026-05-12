# CPU Scheduling Algorithm Demo

在 QEMU 虛擬機器中執行並比較五種 CPU 排程演算法：

- `fcfs`
- `sjf`
- `srtf`
- `priority`
- `rr`

此專案的目的很明確：

1. 用 C 實作常見的 CPU scheduling policies。
2. 用固定 workload 比較各演算法的輸出結果。
3. 用 QEMU + Ubuntu 24.04 建立可重現的執行環境。
4. 產生可閱讀的 demo 輸出與可解析的 benchmark 數據。

這個專案是「排程模擬器（scheduler simulator）」，不是 Linux kernel scheduler modification。  
它不會修改 Linux 核心，也不會用 `ftrace`、`perf`、`eBPF` 收集真實核心排程事件。

## What This Project Actually Does

本專案執行流程如下：

1. `scripts/01_setup_env.sh`
   下載 Ubuntu 24.04 cloud image，建立 VM 磁碟，產生 cloud-init seed ISO。
2. `scripts/02_start_vm.sh`
   啟動 QEMU，等待 SSH 可連線，並確認 VM 內的 scheduler binary 已準備完成。
3. `scripts/03_demo.sh`
   在 VM 內執行 demo workload，輸出每個 process 的統計與 Gantt Chart。
4. `scripts/04_benchmark.sh`
   在 VM 內執行 benchmark workload，擷取平均指標並輸出 CSV。
5. `scripts/05_cleanup.sh`
   關閉 VM 並清除產生的 VM 檔案。

## Algorithms Implemented

### FCFS

First-Come First-Served。  
依 arrival time 排序，先到先執行，不可搶先（non-preemptive）。

### SJF

Shortest Job First。  
在 CPU 空出來時，從所有已到達且尚未完成的 process 中，選 burst time 最小者執行。此專案實作的是不可搶先版本。

### SRTF

Shortest Remaining Time First。  
SJF 的可搶先版本。每個時間單位都重新檢查 ready set，選 remaining time 最小者。

### Priority

Priority Scheduling。  
本專案定義「數字越小，priority 越高」。此版本為不可搶先。

### RR

Round Robin。  
每個 process 最多執行一個 time quantum，再回到 ready queue 尾端。本專案測試 `Q=1`、`Q=2`、`Q=4`、`Q=8`。

## Key Outputs

### Demo output

`results/demo_output.txt` 包含：

- 每個演算法的 process table
- 每個 process 的 `Arrival`、`Burst`、`Start`、`Finish`、`Wait`、`TAT`
- Gantt Chart

### Benchmark output

`results/benchmark.csv` 包含：

- `Algorithm`
- `AWT`：Average Waiting Time
- `ATT`：Average Turnaround Time
- `ART`：Average Response Time

`results/benchmark_report.txt` 會把同一批結果整理成純文字比較表。

## Requirements

Host 環境：

- Ubuntu 24.04 x86_64

腳本會自行檢查並安裝缺少的工具。若要手動安裝，至少需要：

```bash
sudo apt update
sudo apt install -y \
    qemu-system-x86 \
    qemu-utils \
    cloud-image-utils \
    libguestfs-tools \
    gcc \
    wget \
    sshpass \
    bc
```

說明：

- `qemu-system-x86_64`：啟動 VM
- `qemu-img`：建立與轉換磁碟映像
- `cloud-localds`：建立 cloud-init seed ISO
- `virt-customize`：由 setup script 檢查依賴
- `gcc`：在 host 端編譯 `src/scheduler.c`
- `sshpass`：讓 demo/benchmark script 非互動式登入 VM
- `bc`：benchmark script 用來比較浮點數

## Important Implementation Notes

這幾點請先看清楚，避免對專案行為有錯誤理解。

### 1. Scheduler binary 是在 host 編譯，不是在 VM 內編譯

`scripts/01_setup_env.sh` 會先在 host 執行：

```bash
gcc -O2 -Wall -Wextra -std=c11 -o /tmp/scheduler_host_check src/scheduler.c
```

之後把：

- `scheduler` binary
- `scheduler.c`
- `workload_demo.txt`
- `workload_bench.txt`

透過 cloud-init 注入 VM。

因此這個專案的 VM 啟動後，scheduler 已經可執行，不需要在 VM 內再次編譯。

### 2. QEMU machine type 不是固定只有 `q35`

`scripts/02_start_vm.sh` 的實際邏輯是：

- 若 `/dev/kvm` 可讀寫：使用 `-machine q35,accel=kvm`
- 若 KVM 不可用：使用 `-machine pc,accel=tcg`

因此：

- KVM 路徑：`q35`
- TCG 路徑：`pc`

### 3. Benchmark 沒有模擬真實 context-switch cost

Round Robin 與 SRTF 會產生更多切換，但 `src/scheduler.c` 沒有把 context-switch overhead 額外加進時間模型。  
因此 benchmark 顯示的是「排程決策造成的等待與完成結果」，不是「真實作業系統中包含 context switch cost 的完整效能」。

## Quick Start

若你已經在此專案根目錄，只需要：

```bash
chmod +x scripts/*.sh
make all
```

`make all` 會依序執行：

1. `make setup`
2. `make start`
3. `make demo`
4. `make bench`

若你不想啟動 QEMU，只想直接在 host 測試演算法輸出：

```bash
make demo-host
```

這個模式會用 host 端編譯出的 binary，直接執行 `src/workload_demo.txt`。

## Step-by-Step Usage

### 1. Build VM assets

```bash
bash scripts/01_setup_env.sh
```

這支腳本會：

1. 檢查依賴
2. 下載 `noble-server-cloudimg-amd64.img`
3. 建立 `vm/ubuntu2404.qcow2`
4. 產生 `vm/seed.iso`
5. 驗證 `src/scheduler.c` 可編譯
6. 把 scheduler 與 workload 注入 cloud-init

產生的重要檔案：

- `vm/ubuntu2404-base.img`
- `vm/ubuntu2404.qcow2`
- `vm/seed.iso`

### 2. Start the VM

```bash
bash scripts/02_start_vm.sh
```

這支腳本會：

1. 以背景模式啟動 QEMU
2. 建立 host `2222 -> guest 22` 的 SSH port forwarding
3. 等待 SSH 可連線
4. 確認 `/home/scheduler/scheduler` 存在且可執行
5. 確認 cloud-init 已完成

成功後可手動登入：

```bash
ssh -p 2222 scheduler@localhost
```

登入密碼：

```text
scheduler123
```

### 3. Run the demo workload

```bash
bash scripts/03_demo.sh
```

此步驟會執行：

- `fcfs`
- `sjf`
- `srtf`
- `priority`
- `rr 1`
- `rr 2`
- `rr 4`
- `rr 8`

輸出檔案：

- `results/demo_output.txt`

### 4. Run the benchmark workload

```bash
bash scripts/04_benchmark.sh
```

此步驟會：

1. 在 VM 內執行所有演算法
2. 從每次執行結果中擷取 `BENCHMARK ...` 行
3. 解析出 `AWT`、`ATT`、`ART`
4. 寫入 `results/benchmark.csv`
5. 生成 `results/benchmark_report.txt`

### 5. Clean up

```bash
bash scripts/05_cleanup.sh
```

會移除：

- `vm/seed.iso`
- `vm/user-data`
- `vm/meta-data`
- `vm/ubuntu2404.qcow2`
- `vm/qemu.log`
- `vm/qemu-serial.log`

若要連 base image 一起移除：

```bash
bash scripts/05_cleanup.sh --full
```

這會額外刪除：

- `vm/ubuntu2404-base.img`

`results/` 不會被 cleanup script 刪除。

## Input Format

`src/scheduler.c` 的輸入格式固定如下：

```text
<n>
<pid> <arrival> <burst> <priority>
<pid> <arrival> <burst> <priority>
...
```

### Example

```text
6
1 0 8 3
2 1 4 1
3 2 9 4
4 3 5 2
5 4 2 5
6 5 1 3
```

欄位定義：

- `pid`：Process ID
- `arrival`：arrival time
- `burst`：CPU burst time
- `priority`：priority value，數字越小優先權越高

## Output Format

每次執行會輸出兩類資訊。

### 1. Human-readable table

例如：

```text
PID    Arrival  Burst   Start    Finish     Wait     TAT
1      0        8       0        8          0        8
...
```

欄位定義：

- `Start`：第一次取得 CPU 的時間
- `Finish`：完成時間
- `Wait`：waiting time
- `TAT`：turnaround time

### 2. Machine-readable benchmark line

例如：

```text
BENCHMARK FCFS AWT=13.3333 ATT=18.1667 ART=13.3333
```

這一行是 `scripts/04_benchmark.sh` 解析 benchmark 結果的唯一固定格式。  
如果你修改 `scheduler.c` 的輸出格式，請保留這一行的結構，否則 benchmark script 會失敗。

## Result Metrics

### AWT

Average Waiting Time。

公式：

```text
waiting = turnaround - burst
```

### ATT

Average Turnaround Time。

公式：

```text
turnaround = finish - arrival
```

### ART

Average Response Time。

公式：

```text
response = start - arrival
```

## Project Structure

```text
cpu-scheduling-qemu/
├── docs/
│   └── schedule_DEMO_*.png
├── results/
│   ├── benchmark.csv
│   ├── benchmark_report.txt
│   └── demo_output.txt
├── scripts/
│   ├── 01_setup_env.sh
│   ├── 02_start_vm.sh
│   ├── 03_demo.sh
│   ├── 04_benchmark.sh
│   └── 05_cleanup.sh
├── src/
│   ├── scheduler.c
│   ├── workload_bench.txt
│   └── workload_demo.txt
├── Makefile
├── README_schedule.md
└── report.md
```

## Design Choices

### Why x86_64

選 `x86_64` 的原因很直接：

- Ubuntu 24.04 cloud image 直接可用
- `qemu-system-x86_64` 在 Ubuntu 上容易安裝
- 不需要 ARM cross-toolchain

### Why cloud-init

使用 cloud-init 的原因也很直接：

- 不需要手動安裝 VM
- 不需要手動登入後再傳檔
- 每次都能從固定流程重建環境

### Why plain-text workloads

把 workload 放在文字檔而不是寫死在程式裡，有兩個直接好處：

1. 改 workload 不需要改 scheduler code
2. 同一支 scheduler binary 可以重複拿來測不同 workload

## Known Limits

這份專案的限制如下：

1. 單核心模擬，不支援 multi-core scheduling。
2. 時間模型是整數 tick，不是連續時間。
3. 沒有 I/O wait、sleep、wake-up、interrupt-driven state transition。
4. 沒有模擬真實 context-switch cost。
5. `priority` 演算法沒有 aging。
6. 這不是 Linux kernel trace，也不是真實 kernel scheduler benchmark。

## Useful Commands

```bash
make build
make demo-host
make setup
make start
make demo
make bench
make clean
make clean-full
make all
```

## Expected Files After a Full Run

在 `make all` 成功完成、且尚未執行 cleanup 的情況下，應存在：

```text
results/demo_output.txt
results/benchmark.csv
results/benchmark_report.txt
vm/ubuntu2404-base.img
vm/ubuntu2404.qcow2
vm/seed.iso
vm/qemu.pid
vm/qemu.log
vm/qemu-serial.log
```

## Troubleshooting

### `sshpass is required`

請安裝：

```bash
sudo apt install -y sshpass
```

### `Timed out waiting for SSH`

請檢查：

1. `vm/qemu.log`
2. `vm/qemu-serial.log`
3. 主機是否支援 KVM
4. `2222` port 是否已被占用

### `Scheduler binary not found in VM`

代表 cloud-init 尚未成功完成。請重新檢查：

- `scripts/01_setup_env.sh` 是否成功執行
- `scripts/02_start_vm.sh` 是否出現 setup failure 訊息
- `vm/qemu-serial.log` 內容

### `benchmark.csv` 沒有產生內容

請先確認：

1. `results/benchmark_report.txt` 是否有內容
2. `scheduler.c` 是否仍輸出 `BENCHMARK ...` 行
3. `scripts/04_benchmark.sh` 中的解析格式是否仍與程式輸出一致

## Summary

如果只看一句話，這個專案做的事情是：

> 用 C 實作 CPU 排程模擬器，用 QEMU 建立可重現環境，用 shell scripts 自動化 demo 與 benchmark，最後輸出可讀的排程結果與可解析的比較數據。
