# CPU Scheduling Simulator (QEMU Environment) API 技術報告

本報告針對 `/cpu-scheduling-qemu` 子專案進行 Codebase Trace 與架構分析。內容只根據目前實際存在的 `src/scheduler.c`、`Makefile`、`scripts/*.sh`、workload 檔案與 README；其中 source code 優先於 README 與註解。

## 分析標記

- `# Direct Observation`：可直接從目前 codebase 驗證。
- `# Conservative Inference`：僅依據呼叫關係或保守執行語意推論，並明確標示。
- 無法驗證的設計意圖一律標示「目前程式碼中未觀察到」或「無法從現有內容確認」。

---

## 第一階段：Codebase Trace (程式碼追蹤)

### 1. Project Structure (專案結構)

# Direct Observation

- **Source Files**
  - `src/scheduler.c`：核心 CPU scheduling simulator，實作 FCFS、SJF、SRTF、Priority、Round Robin；也包含 input loader、Gantt 記錄、統計輸出與 CLI entry point。

- **Header Files**
  - 目前程式碼中未觀察到專屬 `.h` 檔案。`Process`、`GanttSlot`、macro、function prototype 都直接放在 `src/scheduler.c`。

- **Workload Files**
  - `src/workload_demo.txt`：第一行為 `6`，後續 6 筆 `<pid> <arrival> <burst> <priority>`。
  - `src/workload_bench.txt`：第一行為 `12`，後續 12 筆 `<pid> <arrival> <burst> <priority>`。

- **Build System**
  - `Makefile`：使用 `gcc -O2 -Wall -Wextra -std=c11` 編譯 `src/scheduler.c` 到 `/tmp/scheduler_host`，並提供 `build`、`demo-host`、`setup`、`start`、`demo`、`bench`、`clean`、`clean-full`、`all` target。

- **Scripts**
  - `scripts/01_setup_env.sh`：檢查 host dependencies、下載 Ubuntu cloud image、轉換/resize qcow2、host 端編譯 `scheduler.c`、base64 嵌入 scheduler binary/source/workload 到 cloud-init seed ISO。
  - `scripts/02_start_vm.sh`：啟動 QEMU VM、選擇 KVM 或 TCG、等待 SSH 與 cloud-init ready marker。
  - `scripts/03_demo.sh`：透過 SSH 在 VM 裡執行 demo workload，輸出到 host terminal 並保存 `results/demo_output.txt`。
  - `scripts/04_benchmark.sh`：透過 SSH 在 VM 裡執行 benchmark workload，解析 `BENCHMARK` machine-readable line，輸出 `results/benchmark.csv` 與 `results/benchmark_report.txt`。
  - `scripts/05_cleanup.sh`：停止 QEMU process、刪除 VM artifacts；保留 `results/`。

#### Module / Component Relationship

# Direct Observation

```text
Makefile
  -> build       -> gcc src/scheduler.c -> /tmp/scheduler_host
  -> demo-host   -> /tmp/scheduler_host < src/workload_demo.txt
  -> setup       -> scripts/01_setup_env.sh
  -> start       -> scripts/02_start_vm.sh
  -> demo        -> scripts/03_demo.sh
  -> bench       -> scripts/04_benchmark.sh
  -> clean       -> scripts/05_cleanup.sh

scripts/01_setup_env.sh
  -> gcc src/scheduler.c -> /tmp/scheduler_host_check
  -> base64 scheduler binary/source/workloads
  -> generate cloud-init user-data/meta-data
  -> cloud-localds -> vm/seed.iso

scripts/02_start_vm.sh
  -> qemu-system-x86_64 -daemonize -pidfile vm/qemu.pid
  -> sshpass/ssh polling
  -> wait for /home/scheduler/.setup_done inside VM

scripts/03_demo.sh / 04_benchmark.sh
  -> sshpass/ssh
  -> /home/scheduler/scheduler <algorithm> [quantum] < workload_*.txt
  -> scheduler.c:main()
```

# Conservative Inference

這個子專案的「QEMU」部分主要是可重現執行環境與輸出收集管線；CPU scheduling 的實際演算法 dispatch 與狀態轉移都在 `src/scheduler.c` 的單一 process、單一 thread 內完成。這是由 `scripts/*.sh` 只遠端啟動 `/home/scheduler/scheduler`，而沒有 kernel module、daemon 或多 process simulator code 可驗證出的保守推論。

---

### 2. Semantic Element Extraction (語義要素萃取，只列實際存在)

#### API

# Direct Observation

- C standard library / POSIX-visible API：
  - `scanf()`：`load_processes()` 讀取 `stdin` workload。
  - `printf()` / `fprintf()`：輸出結果、usage、error。
  - `qsort()`：FCFS 與 RR 對 `proc[]` 依 arrival 排序。
  - `strcmp()`：`main()` 依 CLI algorithm 字串 dispatch。
  - `atoi()`：`main()` 解析 Round Robin quantum。
  - `snprintf()`：`sched_rr()` 產生 `RoundRobin_Q%d` label。
  - `exit(1)`：`load_processes()` input error 直接終止 process。

- Shell / external command API：
  - `gcc`、`base64`、`wget`、`qemu-img`、`cloud-localds`、`qemu-system-x86_64`、`sshpass`、`ssh`、`grep`、`awk`、`bc`、`tee`、`kill`、`rm`、`rmdir`。

#### Macro

# Direct Observation

- `MAX_PROC`：定義於 `src/scheduler.c:20`，值為 `64`。
- `WORKLOAD_DEMO` / `WORKLOAD_BENCH`：定義於 `Makefile:21-22`，分別指向 `src/workload_demo.txt` 與 `src/workload_bench.txt`。
- shell constants：
  - `VM_DIR`、`VM_USER`、`VM_PASS`、`SSH_PORT`、`VM_MEM`、`VM_CPUS`、`DISK_SIZE` 等出現在 `scripts/01_setup_env.sh` 與 `scripts/02_start_vm.sh`。
  - `BOOT_TIMEOUT`、`SETUP_TIMEOUT` 定義於 `scripts/02_start_vm.sh`，TCG fallback 時會調整。

#### Inline Function

# Direct Observation

目前 `src/scheduler.c` 未觀察到 `static inline` 或 C inline function。

#### Callback / Function Pointer

# Direct Observation

- `cmp_arrival()`、`cmp_burst()`、`cmp_priority()`：
  - 類型：`qsort()` comparator callback。
  - 定義位置：`src/scheduler.c:123`、`131`、`139`。
  - 實際呼叫：
    - `cmp_arrival` 被 `sched_fcfs()` 與 `sched_rr()` 傳給 `qsort()`。
    - `cmp_burst`、`cmp_priority` 目前定義存在，但目前程式碼中未觀察到傳給 `qsort()` 的呼叫位置。

- Shell helper function：
  - `info()`、`success()`、`warn()`、`die()`：多個 scripts 使用，作為 logging/error helper。
  - `vm_ssh()`：`scripts/02_start_vm.sh` 透過 `sshpass ssh` 對 VM 執行命令。
  - `vm_run()`：`scripts/03_demo.sh` 與 `scripts/04_benchmark.sh` 透過 `sshpass ssh` 對 VM 執行 scheduler。
  - `run_algo()`：`scripts/03_demo.sh` 包裝 demo algorithm dispatch。
  - `run_bench()`、`collect()`、`print_table()`：`scripts/04_benchmark.sh` 包裝 benchmark 執行、收集與輸出。
  - `remove_if_exists()`：`scripts/05_cleanup.sh` 包裝 artifact cleanup。

#### Attribute / Annotation

# Direct Observation

目前 `src/scheduler.c` 未觀察到 `__attribute__`、compiler-specific annotation、linker annotation。

#### Linker / Compiler Annotation

# Direct Observation

目前未觀察到 linker script、section annotation、constructor/destructor attribute。編譯旗標由 `Makefile` 與 `01_setup_env.sh` 指定 `-O2 -Wall -Wextra -std=c11`。

#### Conditional Compilation

# Direct Observation

目前 `src/scheduler.c` 未觀察到 `#ifdef` / `#if` 條件編譯。執行環境條件判斷主要在 shell script，例如 `02_start_vm.sh` 依 `/dev/kvm` 是否可讀寫選擇 KVM 或 TCG。

#### Synchronization Primitive

# Direct Observation

- `src/scheduler.c` 未觀察到 mutex、spinlock、semaphore、condition variable、thread、atomic。
- script 層有 polling/wait 行為：
  - `02_start_vm.sh` 用 `while ! vm_ssh true` 等待 SSH ready。
  - `02_start_vm.sh` 用 `while true` 等待 VM 內 `/home/scheduler/.setup_done`。
  - `05_cleanup.sh` 用 `kill -TERM`、`sleep 3`、`kill -KILL` 管理 QEMU process lifecycle。

# Conservative Inference

scheduler simulator 本體是單執行緒 deterministic simulation；沒有真實 CPU thread concurrency 或 OS scheduler hook。QEMU VM 是執行容器，不是演算法內部的同步模型。

#### Memory Management Mechanism

# Direct Observation

- C simulator：
  - `Process proc[MAX_PROC]`：global static array。
  - `GanttSlot gantt[MAX_PROC * 200]`：global static array，容量 12,800 slots。
  - `done[MAX_PROC]`：`sched_sjf()` / `sched_priority()` stack array。
  - `remaining[MAX_PROC]`、`started[MAX_PROC]`、`in_queue[MAX_PROC]`、`queue[MAX_PROC * 200]`：`sched_rr()` stack arrays。
  - 目前未觀察到 `malloc()` / `free()`。

- Script / VM artifacts：
  - `01_setup_env.sh` 建立 `vm/`、下載 base image、產生 qcow2 disk、cloud-init `user-data`、`meta-data`、`seed.iso`。
  - `05_cleanup.sh` 刪除 `seed.iso`、`user-data`、`meta-data`、`ubuntu2404.qcow2`、logs，`--full` 時刪除 base image。

#### Execution Model

# Direct Observation

- `main()` 先呼叫 `load_processes()`，再依 `argv[1]` 字串呼叫 `sched_fcfs()` / `sched_sjf()` / `sched_srtf()` / `sched_priority()` / `sched_rr(q)`。
- 非搶佔式：
  - `sched_fcfs()`、`sched_sjf()`、`sched_priority()` 以完整 burst 為單位前進 `clock`。
- 搶佔式或 time-slice：
  - `sched_srtf()` 每 tick 重新選最短 remaining process。
  - `sched_rr()` 以 `quantum` 或剩餘時間作為每次 run 長度，使用 queue 模擬 ready queue。

#### Event Dispatch

# Direct Observation

- CLI dispatch：`main()` 使用 `strcmp()` 對 `argv[1]` 做 algorithm dispatch。
- Sort callback dispatch：`qsort()` 使用 comparator function pointer。
- Makefile target dispatch：`make setup/start/demo/bench/clean` 呼叫對應 script。
- Script remote dispatch：`vm_run "/home/${VM_USER}/scheduler ${algo} ${args} < ..."` 在 VM 內執行不同 algorithm。

目前程式碼中未觀察到 event loop、signal handler、timer callback、thread callback。

#### Communication Mechanism

# Direct Observation

- Scheduler input：`stdin`，格式為 `<n>` 後接多行 `<pid> <arrival> <burst> <priority>`。
- Scheduler output：`stdout` human-readable table、Gantt chart、machine-readable `BENCHMARK <label> AWT=<f> ATT=<f> ART=<f>`。
- Host <-> VM：
  - cloud-init seed ISO 將 binary/source/workload 放入 VM。
  - SSH port forward：host `localhost:2222` -> guest port 22。
  - `sshpass ssh` 遠端執行 scheduler。
- Benchmark result：
  - `04_benchmark.sh` 解析 `BENCHMARK` line，寫入 CSV 與文字 report。

#### External Interface

# Direct Observation

- CLI：`scheduler <algorithm> [time_quantum]`。
- Algorithm values：`fcfs`、`sjf`、`srtf`、`priority`、`rr`。
- Input files：`src/workload_demo.txt`、`src/workload_bench.txt`；VM 中則為 `/home/scheduler/workload_demo.txt`、`/home/scheduler/workload_bench.txt`。
- Output files：
  - `results/demo_output.txt`
  - `results/benchmark.csv`
  - `results/benchmark_report.txt`
  - VM artifacts under `vm/`。

#### Registration Mechanism

# Direct Observation

- `qsort()` comparator registration：caller 把 comparator function pointer 傳給 `qsort()`。
- Cloud-init registration：`01_setup_env.sh` 產生 `user-data` / `meta-data` 並用 `cloud-localds` 建立 `seed.iso`。
- QEMU process registration：`02_start_vm.sh` 用 `-pidfile vm/qemu.pid` 保存 QEMU PID。
- Makefile target registration：`.PHONY` 宣告 build/demo/setup/start/demo/bench/clean 等 target。

目前程式碼中未觀察到 kernel module registration、device registration、HTTP route registration、plugin registration。

---

### 3. API / Macro Inventory（分類整理）

#### Initialization

# Direct Observation

| 名稱 | 類型 | 定義位置 | 呼叫位置 / 呼叫來源 | 用途 | 關聯 struct / data | 對 execution flow 的影響 |
| --- | --- | --- | --- | --- | --- | --- |
| `MAX_PROC` | macro | `src/scheduler.c:20` | global array declarations 與 local stack arrays | 限制最大 process 數與 queue/gantt 容量基準 | `proc`、`gantt`、`done`、`queue` | 所有 scheduler storage size 都依此固定。 |
| `proc` | global static array | `src/scheduler.c:45` | `load_processes()` 與所有 `sched_*()` | 保存 process input 與 scheduling stats | `Process` | 是 simulator 的主要 mutable state。 |
| `gantt` | global static array | `src/scheduler.c:46` | `gantt_push()`、`print_gantt()` | 保存 CPU 使用區段 | `GanttSlot` | 決定 Gantt chart output。 |
| `n_proc` | global int | `src/scheduler.c:47` | `load_processes()`、所有 loops | process count | `proc[]` | 控制 scheduler iteration 範圍。 |
| `n_gantt` | global int | `src/scheduler.c:48` | `gantt_push()`、`print_gantt()` | Gantt slot count | `gantt[]` | 控制 Gantt output 範圍。 |
| `load_processes()` | function | `src/scheduler.c:372` | `main()` | 從 `stdin` 初始化 `proc[]` 與 `n_proc` | `proc[]`、`n_proc` | 所有演算法前置資料來源。 |
| `scripts/01_setup_env.sh` constants | shell variables | `scripts/01_setup_env.sh:22-33` | setup script | 設定 image、VM、SSH、disk parameters | `vm/` artifacts | 決定 VM 建置與 cloud-init 內容。 |

#### Registration

# Direct Observation

| 名稱 | 類型 | 定義位置 | 呼叫位置 / 來源 | 用途 | 關聯 data | Flow 影響 |
| --- | --- | --- | --- | --- | --- | --- |
| `qsort(..., cmp_arrival)` | function pointer registration | `sched_fcfs()`、`sched_rr()` | `qsort()` | 註冊 arrival comparator | `proc[]` | 改變 `proc[]` 順序，影響 FCFS/RR execution order。 |
| `cloud-localds "$SEED_ISO" user-data meta-data` | cloud-init seed creation | `scripts/01_setup_env.sh:166` | setup script | 把 cloud-init config 註冊成 seed ISO | `user-data`、`meta-data` | VM 開機後會執行 embedded setup commands。 |
| `qemu-system-x86_64 -pidfile` | process metadata registration | `scripts/02_start_vm.sh:92-107` | start script | 啟動 VM 並保存 PID | `vm/qemu.pid` | cleanup script 依 PID file 找到 QEMU process。 |
| `.PHONY` | Makefile declaration | `Makefile:24` | make | 宣告非檔案 target | target names | 讓 workflow target 不被同名檔案干擾。 |

#### Execution Path

# Direct Observation

| 名稱 | 類型 | 定義位置 | 呼叫來源 | 用途 | 關聯 struct / data | Flow 影響 |
| --- | --- | --- | --- | --- | --- | --- |
| `main()` | CLI entry point | `src/scheduler.c:389` | executable start | 檢查 argv、load input、dispatch algorithm | `argv`、`proc[]` | 決定實際 scheduler path。 |
| `sched_fcfs()` | algorithm function | `src/scheduler.c:149` | `main()` | First-Come First-Served | `proc[]`、`gantt[]` | 依 arrival 排序後線性執行。 |
| `sched_sjf()` | algorithm function | `src/scheduler.c:168` | `main()` | Non-preemptive Shortest Job First | `proc[]`、`done[]` | 每次選已到達且 burst 最短者。 |
| `sched_srtf()` | algorithm function | `src/scheduler.c:207` | `main()` | Preemptive Shortest Remaining Time First | `proc[].remaining` | 每 tick 重新選 shortest remaining。 |
| `sched_priority()` | algorithm function | `src/scheduler.c:250` | `main()` | Non-preemptive Priority Scheduling | `proc[].priority`、`done[]` | 每次選已到達且 priority 數值最小者。 |
| `sched_rr(int quantum)` | algorithm function | `src/scheduler.c:287` | `main()` | Round Robin | local `queue[]`、`remaining[]` | 使用 ready queue 與 time quantum 模擬輪轉。 |
| `gantt_push()` | helper | `src/scheduler.c:53` | all `sched_*()` | 追加或合併 CPU 區段 | `gantt[]`、`n_gantt` | 產生 Gantt chart 的基礎資料。 |
| `compute_stats()` | helper | `src/scheduler.c:65` | all `sched_*()` | 計算 turnaround/waiting/response | `proc[]` | 完成 stats fields。 |
| `print_results()` | output helper | `src/scheduler.c:74` | all `sched_*()` | 輸出表格與 `BENCHMARK` line | `proc[]` | 提供 human-readable 與 machine-readable output。 |
| `print_gantt()` | output helper | `src/scheduler.c:110` | all `sched_*()` | 輸出 Gantt chart | `gantt[]` | 提供視覺化 execution trace。 |

#### Lifecycle

# Direct Observation

| 名稱 | 類型 | 定義位置 | 呼叫來源 | 用途 | 關聯 data | Flow 影響 |
| --- | --- | --- | --- | --- | --- | --- |
| `main()` return | process lifecycle | `src/scheduler.c:389-419` | OS process | 成功回 0；usage/unknown algorithm 回 1 | executable process | 結束 scheduler run。 |
| `exit(1)` | error termination | `load_processes()` | input parse failure | 立即結束 process | `stdin`、`proc[]` | 不會進入 scheduling/reporting。 |
| `scripts/02_start_vm.sh` wait loops | VM lifecycle | start script | user / Makefile | 等待 VM SSH 與 cloud-init ready | `vm/qemu.pid`、SSH | 確保 demo/bench 前 VM 內 scheduler 可執行。 |
| `scripts/05_cleanup.sh` | cleanup lifecycle | cleanup script | user / Makefile | 停止 QEMU、刪除 VM artifacts | `vm/` files | 結束 QEMU environment；保留 results。 |

#### Memory Handling

# Direct Observation

| 名稱 | 類型 | 定義位置 | 呼叫來源 | 用途 | 關聯 data | Flow 影響 |
| --- | --- | --- | --- | --- | --- | --- |
| `Process proc[MAX_PROC]` | global static storage | `src/scheduler.c:45` | all scheduler code | 保存 process state | `Process` | 不需 allocation/free，但受 `MAX_PROC` 限制。 |
| `GanttSlot gantt[MAX_PROC * 200]` | global static storage | `src/scheduler.c:46` | `gantt_push()` | 保存 Gantt slots | `GanttSlot` | 容量固定，沒有邊界檢查。 |
| `done[MAX_PROC]` | stack array | `sched_sjf()`、`sched_priority()` | non-preemptive algorithms | 記錄完成狀態 | process index | function return 後釋放。 |
| `remaining/start/in_queue/queue` | stack arrays | `sched_rr()` | Round Robin | 模擬 ready queue 與剩餘時間 | process index | function return 後釋放。 |
| VM artifacts | filesystem files | `scripts/01_setup_env.sh` | setup | image、disk、seed ISO、cloud-init data | `vm/` | 由 cleanup script 刪除。 |

#### Synchronization

# Direct Observation

| 名稱 | 類型 | 定義位置 | 呼叫來源 | 用途 | 關聯 data | Flow 影響 |
| --- | --- | --- | --- | --- | --- | --- |
| `while ! vm_ssh true` | polling loop | `scripts/02_start_vm.sh:122` | start script | 等待 SSH ready | VM state | 阻擋後續 demo/bench 直到 VM 可連線。 |
| cloud-init ready polling | polling loop | `scripts/02_start_vm.sh:141-165` | start script | 等待 `/home/scheduler/.setup_done` | VM file marker | 確保 scheduler binary 已在 VM 中就緒。 |
| `kill -0` | process existence check | `scripts/02_start_vm.sh`、`05_cleanup.sh` | start/cleanup scripts | 檢查 QEMU PID 是否仍活著 | `vm/qemu.pid` | 避免重複啟動或清理錯誤 PID。 |

目前 `src/scheduler.c` 中未觀察到同步原語，因為 simulator 沒有 thread 或 shared-memory concurrency。

#### Event Dispatch

# Direct Observation

| 名稱 | 類型 | 定義位置 | 呼叫來源 | 用途 | Flow 影響 |
| --- | --- | --- | --- | --- | --- |
| `strcmp()` dispatch | string dispatch | `main()` | CLI argv | 選擇 scheduler algorithm | 進入對應 `sched_*()`。 |
| `qsort()` comparator | callback dispatch | `sched_fcfs()`、`sched_rr()` | C library | 排序 `proc[]` | 改變 algorithm state order。 |
| `run_algo()` | shell dispatch helper | `scripts/03_demo.sh` | demo script | 執行 VM 內 scheduler | demo output path。 |
| `run_bench()` / `collect()` | shell dispatch helper | `scripts/04_benchmark.sh` | benchmark script | 執行並解析 VM 內 scheduler | CSV/report output path。 |
| Makefile target | build/workflow dispatch | `Makefile` | `make` | 串接 scripts | 控制 host workflow。 |

#### Logging / Debug

# Direct Observation

- `scheduler.c`
  - usage/error：`fprintf(stderr, ...)`。
  - result/debug-readable output：`printf()` table、Gantt chart。
  - machine-readable output：`BENCHMARK` line。
- scripts
  - colored `info/success/warn/die` helper。
  - QEMU logs：`vm/qemu.log`、`vm/qemu-serial.log`。
  - result logs：`results/demo_output.txt`、`results/benchmark_report.txt`、`results/benchmark.csv`。

#### Error Handling

# Direct Observation

| 名稱 / path | 定義位置 | Error behavior |
| --- | --- | --- |
| missing CLI algorithm | `main()` | 印 usage，return 1。 |
| unknown algorithm | `main()` | 印 unknown algorithm，return 1。 |
| input parse failure | `load_processes()` | `fprintf(stderr, ...)` 後 `exit(1)`。 |
| missing dependencies | `01_setup_env.sh` | 嘗試 apt install；失敗 `die()`。 |
| host compile failure | `01_setup_env.sh` | `die "scheduler.c failed to compile"`。 |
| missing workload | `01_setup_env.sh` | `die`。 |
| missing disk/seed/sshpass | `02_start_vm.sh` | `die`。 |
| QEMU start failure | `02_start_vm.sh` | `die` 並提示 log。 |
| SSH timeout / cloud-init timeout | `02_start_vm.sh` | `die` 並提示 log。 |
| demo/bench VM scheduler missing | `03_demo.sh`、`04_benchmark.sh` | `die`。 |
| cleanup process still running | `05_cleanup.sh` | 先 `TERM`，仍存在則 `KILL`。 |

目前 `src/scheduler.c` 未觀察到 `n_proc > MAX_PROC`、`n_proc == 0`、negative burst/arrival、Round Robin quantum <= 0 的 validation。

#### Cleanup

# Direct Observation

- Scheduler process：不做 dynamic memory cleanup，process exit 後由 OS 回收 static/stack storage。
- QEMU environment：
  - `05_cleanup.sh` 停止 QEMU PID。
  - 刪除 seed ISO、cloud-init generated files、qcow2 disk、logs。
  - `--full` 才刪除 base image。
  - `results/` 明確保留。

---

### 4. Call Graph (呼叫圖譜)

#### Initialization Chain

# Direct Observation

```text
[Host build path]
make build
  -> gcc -O2 -Wall -Wextra -std=c11 -o /tmp/scheduler_host src/scheduler.c

[QEMU setup path]
make setup
  -> scripts/01_setup_env.sh
     -> dependency check
     -> wget Ubuntu cloud image
     -> qemu-img convert/resize
     -> gcc src/scheduler.c -> /tmp/scheduler_host_check
     -> base64 scheduler.c / scheduler binary / workloads
     -> generate user-data and meta-data
     -> cloud-localds seed.iso user-data meta-data

[VM start path]
make start
  -> scripts/02_start_vm.sh
     -> validate disk and seed ISO
     -> choose KVM if /dev/kvm readable/writable, else TCG
     -> qemu-system-x86_64 -daemonize -pidfile vm/qemu.pid
     -> wait for SSH
     -> wait for /home/scheduler/.setup_done
```

#### Runtime Chain

# Direct Observation

```text
[Scheduler executable]
main(argc, argv)
  -> if argc < 2: print usage and return 1
  -> load_processes()
       -> scanf n_proc
       -> scanf pid/arrival/burst/priority for each process
       -> initialize remaining/start/responded
  -> algo = argv[1]
  -> strcmp dispatch:
       fcfs     -> sched_fcfs()
       sjf      -> sched_sjf()
       srtf     -> sched_srtf()
       priority -> sched_priority()
       rr       -> atoi(argv[2] or default 2) -> sched_rr(q)
  -> sched_*()
       -> scheduling loop mutates proc[]
       -> gantt_push()
       -> compute_stats()
       -> print_results()
       -> print_gantt()
  -> return 0
```

#### Cleanup Chain

# Direct Observation

```text
make clean
  -> scripts/05_cleanup.sh
     -> if vm/qemu.pid exists:
          -> read QEMU_PID
          -> kill -0 QEMU_PID
          -> kill -TERM QEMU_PID
          -> sleep 3
          -> if still alive: kill -KILL QEMU_PID
          -> rm -f vm/qemu.pid
     -> rm -f seed.iso user-data meta-data ubuntu2404.qcow2 qemu.log qemu-serial.log
     -> if --full: rm -f ubuntu2404-base.img
     -> if vm/ empty: rmdir vm/
     -> retain results/
```

#### Callback Chain

# Direct Observation

```text
sched_fcfs()
  -> qsort(proc, n_proc, sizeof(Process), cmp_arrival)
       -> qsort invokes cmp_arrival(const void *, const void *)

sched_rr()
  -> qsort(proc, n_proc, sizeof(Process), cmp_arrival)
       -> qsort invokes cmp_arrival(const void *, const void *)

scripts/03_demo.sh
  -> run_algo(algo, label, args)
       -> vm_run(command)
            -> sshpass ssh scheduler@localhost command

scripts/04_benchmark.sh
  -> collect(label, algo, quantum)
       -> run_bench(algo, quantum)
            -> vm_run(command)
            -> grep '^BENCHMARK'
            -> awk / grep -oP parse metrics
```

#### Indirect Call Chain

# Direct Observation

| Indirect mechanism | Function pointer / dispatch key | Target | Trigger |
| --- | --- | --- | --- |
| CLI string dispatch | `argv[1] == "fcfs"` | `sched_fcfs()` | `scheduler fcfs` |
| CLI string dispatch | `argv[1] == "sjf"` | `sched_sjf()` | `scheduler sjf` |
| CLI string dispatch | `argv[1] == "srtf"` | `sched_srtf()` | `scheduler srtf` |
| CLI string dispatch | `argv[1] == "priority"` | `sched_priority()` | `scheduler priority` |
| CLI string dispatch | `argv[1] == "rr"` | `sched_rr(q)` | `scheduler rr [q]` |
| `qsort()` callback | `cmp_arrival` | comparator body | FCFS / RR sorting |
| Make target | `demo` | `bash scripts/03_demo.sh` | `make demo` |
| Make target | `bench` | `bash scripts/04_benchmark.sh` | `make bench` |
| SSH remote command | command string | VM scheduler executable | `vm_run` / `vm_ssh` |

目前程式碼中未觀察到 C-level operation table、vtable、plugin dispatch table、signal callback。

---

### 5. Struct / Resource Tracing

#### `Process`

# Direct Observation

- 定義位置：`src/scheduler.c:22-35`。
- 欄位：
  - input fields：`pid`、`arrival`、`burst`、`priority`。
  - mutable scheduling fields：`remaining`、`start`、`finish`、`waiting`、`turnaround`、`response`、`responded`。
- allocation / init：
  - global `Process proc[MAX_PROC]`。
  - `load_processes()` 透過 `scanf()` 填入 input fields。
  - `load_processes()` 設定 `remaining = burst`、`start = -1`、`responded = 0`。
- ownership：
  - scheduler process global storage owns `proc[]`。
  - 各 `sched_*()` 直接 mutates `proc[]`。
- lifetime：
  - process 啟動後 static storage 存在到 process exit。
- state transition：
  - input loaded：`arrival/burst/priority` 確定。
  - selected：`start` 被設定為 clock。
  - running：
    - non-preemptive：直接設定 `finish = clock + burst`。
    - SRTF/RR：`remaining` 或 local `remaining[]` 遞減。
  - completed：`finish` 設定完成。
  - stats：`compute_stats()` 設定 `turnaround/waiting/response`。
- data passing path：
  - `stdin` -> `scanf()` -> `proc[]` -> `sched_*()` -> `print_results()`。
- callback binding：
  - `proc[]` 被 `qsort()` 透過 comparator callback 讀取與重排。

#### `GanttSlot`

# Direct Observation

- 定義位置：`src/scheduler.c:37-41`。
- 欄位：`pid`、`start`、`end`。
- allocation / init：
  - global `GanttSlot gantt[MAX_PROC * 200]`。
  - `n_gantt` 初始為 0。
- ownership：
  - scheduler process global storage owns `gantt[]`。
- lifetime：
  - process lifetime。
- state transition：
  - `gantt_push()` 若上一段 `pid` 相同，延長上一個 slot 的 `end`。
  - 否則新增 slot 並遞增 `n_gantt`。
- release timing：
  - process exit 後由 OS 回收。
- data passing path：
  - `sched_*()` -> `gantt_push()` -> `gantt[]` -> `print_gantt()`。

#### Round Robin Queue Resources

# Direct Observation

- 定義位置：`sched_rr()` local stack arrays。
- resources：
  - `remaining[MAX_PROC]`
  - `started[MAX_PROC]`
  - `in_queue[MAX_PROC]`
  - `queue[MAX_PROC * 200]`
  - `q_head` / `q_tail`
- allocation / init：
  - function entry 時 stack allocation。
  - `qsort(proc, ..., cmp_arrival)` 後初始化每個 process 的 remaining/start/started/in_queue。
  - arrival == 0 的 process 先 enqueue。
- ownership：
  - `sched_rr()` function owns local queue state。
- lifetime：
  - 只在 `sched_rr()` call 期間存在。
- state transition：
  - enqueue process index -> `q_tail++`。
  - dequeue process index -> `q_head++`。
  - time slice 後若未完成，重新 enqueue current index。
- release timing：
  - `sched_rr()` return 後 stack storage 無效。

#### VM Artifacts

# Direct Observation

- 定義 / 建立：
  - `vm/ubuntu2404-base.img`：`wget` 下載。
  - `vm/ubuntu2404.qcow2`：`qemu-img convert/resize` 建立。
  - `vm/user-data`、`vm/meta-data`：`01_setup_env.sh` 產生。
  - `vm/seed.iso`：`cloud-localds` 產生。
  - `vm/qemu.pid`：QEMU `-pidfile` 產生。
  - `vm/qemu.log`、`vm/qemu-serial.log`：start script 建立/截斷後由 QEMU 寫入。
- ownership：
  - host filesystem owns artifacts；scripts 管理 lifecycle。
- release timing：
  - `05_cleanup.sh` 移除 generated VM files。
  - base image 只有 `--full` 才移除。
  - `results/` intentionally kept。

---

### 6. Execution Trace (執行追蹤)

#### Initialization Flow

# Direct Observation

```text
[scheduler process]
OS starts scheduler
  -> main()
  -> validate argc >= 2
  -> load_processes()
       -> read n_proc
       -> for each process:
            read pid arrival burst priority
            remaining = burst
            start = -1
            responded = 0
```

#### Runtime Flow

# Direct Observation

```text
[FCFS]
sched_fcfs()
  -> qsort by arrival
  -> clock = 0
  -> for each proc:
       if clock < arrival: clock = arrival
       start = clock
       finish = clock + burst
       gantt_push(pid, start, finish)
       clock = finish
  -> compute_stats()
  -> print_results("FCFS")
  -> print_gantt()

[SJF / Priority non-preemptive]
while completed < n_proc:
  -> scan all not-done processes with arrival <= clock
  -> choose shortest burst OR lowest priority
  -> if none ready: clock = next arrival
  -> run selected process to completion
  -> mark done and completed++

[SRTF]
while completed < n_proc:
  -> scan all arrived processes with remaining > 0
  -> choose shortest remaining
  -> if none ready: clock++
  -> if first selected: start = clock
  -> gantt_push(pid, clock, clock + 1)
  -> remaining--
  -> clock++
  -> if remaining == 0: finish = clock; completed++

[RR]
qsort by arrival
initialize local remaining/started/in_queue/queue
enqueue arrival == 0
while completed < n_proc:
  -> if queue empty: clock = next arrival; enqueue newly arrived
  -> dequeue idx
  -> if first selected: start = clock
  -> run = min(remaining[idx], quantum)
  -> gantt_push(pid, clock, clock + run)
  -> clock += run
  -> remaining[idx] -= run
  -> enqueue newly arrived
  -> if remaining == 0: finish = clock; completed++
     else: re-enqueue current idx
```

#### Cleanup Flow

# Direct Observation

```text
[scheduler process]
print output
  -> main returns 0
  -> OS reclaims process memory

[QEMU environment]
scripts/05_cleanup.sh
  -> stop QEMU by PID file
  -> remove generated vm files
  -> optionally remove base image
  -> retain results/
```

#### Data Flow

# Direct Observation

```text
workload file or stdin
  -> scanf in load_processes()
  -> proc[]
  -> sched_* mutates start/finish/remaining
  -> compute_stats() derives waiting/turnaround/response
  -> print_results()
       -> human table
       -> BENCHMARK line
  -> scripts/04_benchmark.sh
       -> grep '^BENCHMARK'
       -> parse AWT/ATT/ART
       -> results/benchmark.csv
       -> results/benchmark_report.txt
```

#### Event Flow

# Direct Observation

```text
CLI event:
  argv[1] chooses algorithm branch

Scheduling event:
  clock and arrival determine ready set
  selected process changes proc[].start / finish / remaining
  gantt_push records CPU occupancy interval

Script event:
  VM reaches SSH ready
  VM creates .setup_done marker
  demo/bench scripts start remote scheduler runs
  cleanup script sees qemu.pid and terminates QEMU process
```

#### Ownership Transfer

# Direct Observation

```text
stdin workload bytes
  -> copied into proc[] by scanf
  -> no pointer ownership transfer

proc[] / gantt[]
  -> owned by scheduler process
  -> mutated by algorithm functions
  -> read by output functions

scheduler binary/source/workload
  -> host files
  -> base64 text embedded in cloud-init user-data
  -> decoded inside VM into /home/scheduler/

VM artifacts
  -> created by setup/start scripts
  -> removed by cleanup script
```

---

## 第二階段：Architecture / API Technical Report

### 1. Execution Semantics (執行語義)

# Direct Observation

`src/scheduler.c` 的 entry point 是 `main()`。它先載入 input，再用 `strcmp()` 分派 algorithm：

- `fcfs`：`sched_fcfs()`，先依 arrival 排序，再完整執行每個 process。
- `sjf`：`sched_sjf()`，non-preemptive，每次從 ready set 選 burst 最短者。
- `srtf`：`sched_srtf()`，preemptive，每個 tick 重新掃描 ready set。
- `priority`：`sched_priority()`，non-preemptive，每次從 ready set 選 priority 數值最小者。
- `rr`：`sched_rr(q)`，使用 local queue 與 quantum。

所有 algorithm 都共享：

```text
sched_*()
  -> mutate proc[] and gantt[]
  -> compute_stats()
  -> print_results()
  -> print_gantt()
```

# Conservative Inference

本 simulator 的時間不是 wall-clock，也不是 QEMU guest kernel scheduler 的真實事件；它是 `int clock` 在 C code 中被演算法手動推進的離散時間模型。QEMU 只提供隔離執行環境，沒有參與調度決策。

---

### 2. Callback Registration Chain

# Direct Observation

目前 C 層唯一實際觀察到的 callback registration 是 `qsort()` comparator：

```text
sched_fcfs()
  -> qsort(proc, n_proc, sizeof(Process), cmp_arrival)

sched_rr()
  -> qsort(proc, n_proc, sizeof(Process), cmp_arrival)
```

`cmp_arrival()` 會先比較 `arrival`，相同時比較 `pid`。這會影響 FCFS 與 RR 的初始 process order。

`cmp_burst()` 與 `cmp_priority()` 目前只被定義，未觀察到被 `qsort()` 或其他 function pointer 使用。SJF 與 Priority 實作不是透過 sort callback，而是在 while loop 裡每次線性掃描 `proc[]`。

Shell 層的 callback-like chain 是 function wrapper：

```text
03_demo.sh: run_algo()
  -> vm_run()
  -> sshpass ssh command

04_benchmark.sh: collect()
  -> run_bench()
  -> vm_run()
  -> parse BENCHMARK
```

這些是 shell function call，不是 C function pointer。

---

### 3. Runtime Dispatch Flow / Indirect Call Path

# Direct Observation

#### CLI dispatch

`main()` 使用 `strcmp()` cascade：

```text
argv[1] == "fcfs"     -> sched_fcfs()
argv[1] == "sjf"      -> sched_sjf()
argv[1] == "srtf"     -> sched_srtf()
argv[1] == "priority" -> sched_priority()
argv[1] == "rr"       -> atoi(argv[2] or 2) -> sched_rr(q)
otherwise             -> return 1
```

這裡沒有 dispatch table，也沒有 function pointer table；所有 algorithm branch 都是 explicit `if/else if`。

#### Remote dispatch

`03_demo.sh` 與 `04_benchmark.sh` 不直接執行 host `/tmp/scheduler_host`，而是使用：

```text
sshpass -p "$VM_PASS" ssh ... "scheduler@localhost" \
  "/home/scheduler/scheduler ${algo} ${args} < /home/scheduler/workload_*.txt"
```

因此 demo/benchmark runtime path 是：

```text
host script -> SSH localhost:2222 -> QEMU guest -> scheduler executable -> scheduler.c:main()
```

#### Benchmark parsing dispatch

`04_benchmark.sh` 對每次 scheduler output：

```text
raw=$(vm_run ...)
bench_line=$(echo "$raw" | grep '^BENCHMARK')
label=$(echo "$bench_line" | awk '{print $2}')
awt=$(echo "$bench_line" | grep -oP 'AWT=\K[\d.]+')
att=$(echo "$bench_line" | grep -oP 'ATT=\K[\d.]+')
art=$(echo "$bench_line" | grep -oP 'ART=\K[\d.]+')
```

所以 `print_results()` 的 `BENCHMARK` line 是 scripts 的 external interface contract。

---

### 4. Resource Lifecycle / Ownership Transition

# Direct Observation

#### Scheduler process resources

- `proc[]` 與 `gantt[]` 是 global static storage。
- 所有 algorithm function 直接修改 `proc[]`；沒有複製一份 immutable input。
- `gantt[]` 只會 append/merge，不會在同一個 process run 中 reset；但目前 `main()` 每次只 dispatch 一個 algorithm，因此單次執行不會跨 algorithm 重用 `gantt[]`。
- `compute_stats()` 在 algorithm 完成後覆寫 `waiting`、`turnaround`、`response`。
- 程式沒有 `malloc/free`，process exit 後由 OS 回收所有 storage。

#### VM resources

- `01_setup_env.sh` 建立 `vm/` 與 QEMU/cloud-init artifacts。
- `02_start_vm.sh` 啟動 QEMU 並保存 PID。
- `03_demo.sh` / `04_benchmark.sh` 只使用 SSH 執行 VM 內 scheduler，不管理 VM file lifecycle。
- `05_cleanup.sh` 停止 QEMU process 並刪除 generated VM artifacts；保留 `results/`。

# Conservative Inference

因為 `proc[]` 會被 `qsort()` 重排、各 algorithm 也會寫入 `start/finish/remaining`，如果未來改成同一個 process 內連續跑多個 algorithm，必須先 reset `proc[]`、`gantt[]`、`n_gantt`。目前 CLI 一次只跑一個 algorithm，所以現有流程中沒有直接暴露這個問題。

---

### 5. State Transition Analysis

# Direct Observation

#### Common process state

```text
loaded:
  pid/arrival/burst/priority set by scanf
  remaining = burst
  start = -1
  responded = 0

scheduled:
  start set when first CPU access occurs

completed:
  finish set

reported:
  turnaround = finish - arrival
  waiting = turnaround - burst
  response = start - arrival
```

`responded` 欄位目前只在 `load_processes()` 設為 0，未觀察到後續被使用。

#### Algorithm-specific state

- FCFS：
  - `proc[]` 被 `qsort()` 重排。
  - 每個 process 一次完成。

- SJF：
  - `done[]` 表示 process 是否完成。
  - 若沒有 ready process，`clock` 直接跳到下一個未完成 process 的 arrival。

- SRTF：
  - `proc[i].remaining` 每 tick 遞減。
  - 若沒有 ready process，`clock++`。

- Priority：
  - `done[]` 表示 process 是否完成。
  - priority 數值越小越優先。

- RR：
  - local `remaining[]` 保存剩餘時間，沒有使用 `proc[i].remaining` 進行 RR 進度。
  - `started[]` 同時被用來表示已開始執行；enqueue newly arrived 時條件是 `!started[i]`。
  - `in_queue[]` 設為 1 後目前未觀察到在 dequeue 或完成時清回 0。

# Conservative Inference

RR 的 `in_queue[]` 在 process 首次 enqueue 後不會清掉，這讓「新到達 process 只 enqueue 一次」成立；current process 的 re-enqueue 不依賴 `in_queue[]`。此設計可從目前程式碼運作推得，但註解沒有明說。

---

### 6. Error Propagation Path

# Direct Observation

#### `scheduler.c`

```text
argc < 2
  -> fprintf usage
  -> return 1

scanf n_proc failed
  -> fprintf "Input error"
  -> exit(1)

scanf process fields failed
  -> fprintf "Input error on process %d"
  -> exit(1)

unknown algorithm
  -> fprintf "Unknown algorithm"
  -> return 1

valid algorithm
  -> sched_*()
  -> return 0
```

目前 `scheduler.c` 中沒有檢查：

- `n_proc <= MAX_PROC`
- `n_proc > 0`
- `arrival >= 0`
- `burst > 0`
- `priority` 合理範圍
- `rr` quantum > 0

#### Scripts

- `set -euo pipefail` 存在於所有 `scripts/*.sh`。
- `die()` helper 會印 error 並 `exit 1`。
- `01_setup_env.sh` 對 dependency install、host compile、workload missing 有明確 failure path。
- `02_start_vm.sh` 對 missing disk/seed、QEMU start failure、SSH timeout、cloud-init failure、kernel panic log 有明確 failure path。
- `03_demo.sh` 與 `04_benchmark.sh` 先檢查 VM 內 scheduler binary 是否存在。
- `04_benchmark.sh` 假設 scheduler output 一定含 `BENCHMARK` line；若 `grep '^BENCHMARK'` 失敗，在 `set -e` 下 `run_bench()` 會中止。

---

### 7. 比較分析

#### 類似 API 行為：五個 `sched_*()` 函式

# Direct Observation

| 項目 | FCFS | SJF | SRTF | Priority | RR |
| --- | --- | --- | --- | --- | --- |
| entry | `sched_fcfs()` | `sched_sjf()` | `sched_srtf()` | `sched_priority()` | `sched_rr(q)` |
| pre-sort | `qsort(..., cmp_arrival)` | 無 | 無 | 無 | `qsort(..., cmp_arrival)` |
| ready selection | sorted order | min `burst` among arrived | min `remaining` among arrived | min `priority` among arrived | queue head |
| preemption | 無 | 無 | 每 tick 可重選 | 無 | quantum 後 re-enqueue |
| idle handling | `clock = arrival` | `clock = next arrival` | `clock++` | `clock = next arrival` | queue empty 時 `clock = next arrival` |
| Gantt granularity | whole burst | whole burst | 1 tick | whole burst | `min(remaining, quantum)` |
| stats path | common | common | common | common | common |

#### Callback 機制差異

# Direct Observation

- FCFS/RR 使用 `qsort()` comparator callback 做初始 arrival ordering。
- SJF/Priority 沒有使用 `cmp_burst()` / `cmp_priority()` callback，而是在 runtime 每輪掃描 ready set。
- SRTF 沒有 callback；每 tick 直接掃描 `proc[]`。

# Conservative Inference

SJF/Priority 選擇線性掃描而非 priority queue 或 `qsort()`，是因為 ready set 會受到 `clock` 與 `done[]` 影響；每輪掃描能直接處理 arrival constraint。這是由目前 code flow 推得，無法從註解確認是原始設計原因。

#### Dispatch Model

# Direct Observation

- C 層 algorithm dispatch 是 `strcmp()` cascade。
- Sort dispatch 是 `qsort()` callback。
- Host workflow dispatch 是 Makefile target。
- VM execution dispatch 是 SSH command string。
- Benchmark result dispatch 是 `BENCHMARK` text parsing。

目前程式碼中未觀察到 C dispatch table，例如 `struct { const char *name; void (*fn)(void); }`。

#### Resource Management Model

# Direct Observation

| 層級 | Resource model | Release model |
| --- | --- | --- |
| C scheduler | static/global arrays + stack arrays | process exit / stack unwind |
| Make build | `/tmp/scheduler_host` | 目前 Makefile `clean` 不移除 `/tmp/scheduler_host`，它只呼叫 VM cleanup script |
| VM setup | `vm/` image/seed/log files | `05_cleanup.sh` |
| Results | `results/*.txt`、`results/*.csv` | cleanup script 明確保留 |

# Conservative Inference

此設計偏向「可重跑的 demo/benchmark pipeline」，不是長駐服務；資源釋放主要依賴 process exit 與 cleanup script，而不是 C code 內的 destructor 或 allocator discipline。

---

### 8. Debug / Risk Analysis

#### Potential Memory Leak

# Direct Observation

- `scheduler.c` 沒有 `malloc()` / `free()`，C heap leak 目前程式碼中未觀察到。
- `05_cleanup.sh` 會清除 VM generated artifacts，但明確保留 `results/`。
- `Makefile` 的 `build` 產物 `/tmp/scheduler_host` 不在 `05_cleanup.sh` 的清理範圍。

# Conservative Inference

- 嚴格來說 `/tmp/scheduler_host` 是 host temporary binary artifact，不是 C memory leak；但重複 build 可能留下舊 binary，cleanup target 不會處理它。
- 若 `01_setup_env.sh` 在建立 `vm/user-data`、`seed.iso` 前後失敗，部分 VM artifacts 可能殘留，需靠 `05_cleanup.sh` 清掉。

#### Invalid Ownership Transfer

# Direct Observation

- `proc[]` 與 `gantt[]` 都是 global storage，不傳出 pointer 給外部 process。
- Host 將 scheduler binary/source/workload base64 嵌入 cloud-init，再在 VM 內 decode 成 `/home/scheduler/*`。

# Conservative Inference

- cloud-init `user-data` 內含由 `VM_PASS` 產生的 password hash 與 base64 binary/source/workloads；這是 deployment artifact，不是 runtime ownership transfer。若 `vm/user-data` 未清理，會留下可檢視的 provisioning 資料。

#### Callback Misuse Risk

# Direct Observation

- `cmp_burst()` 與 `cmp_priority()` 定義存在但目前未被使用。
- `gantt_push()` 沒有檢查 `n_gantt < MAX_PROC * 200`。
- `qsort()` comparator 以 subtraction 回傳差值，例如 `pa->arrival - pb->arrival`。

# Conservative Inference

- 在目前小型 workload 下 subtraction comparator 不太會出現 overflow；但若輸入極大 int 值，仍有 comparator overflow 風險。
- 若未來把 `cmp_burst()` / `cmp_priority()` 誤認為已參與 SJF/Priority runtime dispatch，會誤讀實作。實際 SJF/Priority 是 loop scan，不是 callback sort。

#### Lifecycle Mismatch

# Direct Observation

- `scheduler.c` 單次 process 只跑一個 algorithm；`gantt[]` 和 `n_gantt` 沒有 reset API。
- `Makefile clean` 呼叫 `scripts/05_cleanup.sh`，不移除 `/tmp/scheduler_host`。
- `02_start_vm.sh` 若發現 PID file 指向仍在跑的 process，會提示 VM already running 並 exit 0。

# Conservative Inference

- 如果 PID file 指向非 QEMU 但仍存在的 process，`02_start_vm.sh` 只用 `kill -0` 判斷，無法確認該 PID 真的是本專案 QEMU。`05_cleanup.sh` 也可能對該 PID 發 `TERM/KILL`。目前程式碼中未觀察到 command-line 或 process name 驗證。

#### Concurrency Issue

# Direct Observation

- C simulator 本體沒有 thread，也沒有 shared mutable state 被多 thread 同時存取。
- QEMU lifecycle 由 PID file 管理，但 scripts 沒有 file lock。

# Conservative Inference

- 若同時開兩個 terminal 執行 `02_start_vm.sh` 或 `05_cleanup.sh`，PID file 與 artifact cleanup 可能 race。這不是 scheduler algorithm concurrency，而是 host script lifecycle concurrency 風險。

#### Data / Bounds Risk

# Direct Observation

- `load_processes()` 讀入 `n_proc` 後直接 `for (int i = 0; i < n_proc; i++)` 寫 `proc[i]`，未檢查 `n_proc <= MAX_PROC`。
- `print_results()` 以 `sum / n_proc` 輸出 average，未檢查 `n_proc > 0`。
- `sched_rr()` 使用 `atoi(argv[2])`，未檢查 quantum 是否大於 0。
- `queue[MAX_PROC * 200]` 與 `gantt[MAX_PROC * 200]` 都沒有 push 邊界檢查。

# Conservative Inference

- `n_proc > 64` 會造成 `proc[]` out-of-bounds write。
- `n_proc == 0` 可能造成 division by zero 或 algorithm state 不完整。
- `rr 0` 或 negative quantum 會讓 `run = min(remaining, quantum)` 變成 0 或負值，可能導致 clock 不前進、remaining 不下降或狀態錯亂。
- 在 quantum 很小、burst 總量很大時，`gantt[]` 或 RR `queue[]` 可能 overflow；既有 report 提到 Gantt overflow，依目前 code 可直接驗證沒有邊界檢查。

#### Error Propagation / Parsing Risk

# Direct Observation

- `04_benchmark.sh` 使用 `grep '^BENCHMARK'`，再用 `awk` 與 `grep -oP` 解析。
- `print_results()` 固定輸出 `BENCHMARK %s AWT=%.4f ATT=%.4f ART=%.4f`。

# Conservative Inference

- `BENCHMARK` line 是 benchmark script 與 C output 的隱含 contract；若 label 格式含空白，`awk '{print $2}'` 只會取第二欄。目前 labels 使用 `FCFS`、`SJF_NonPreemptive`、`SRTF_Preemptive`、`Priority_NonPreemptive`、`RoundRobin_Q%d`，都不含空白，所以目前可運作。

---

## 結論

# Direct Observation

`/cpu-scheduling-qemu` 目前是一個單檔 C scheduler simulator，加上一組 QEMU/cloud-init/SSH automation scripts。核心 API surface 是：

- CLI：`scheduler <algorithm> [time_quantum]`
- stdin workload：`<n>` + `<pid> <arrival> <burst> <priority>`
- stdout machine-readable line：`BENCHMARK <label> AWT=<f> ATT=<f> ART=<f>`
- scripts：`setup -> start -> demo -> bench -> cleanup`

主要 execution semantics 由 `main()` 的 `strcmp()` dispatch 進入五個 `sched_*()`，所有演算法共用 `proc[]`、`gantt[]`、`compute_stats()`、`print_results()`、`print_gantt()`。唯一 C-level callback chain 是 `qsort()` 使用 `cmp_arrival()`；SJF、SRTF、Priority 的 runtime 選擇邏輯都是直接掃描 `proc[]`。

# Conservative Inference

這份 code 適合展示單 CPU、離散時間、單次執行的 scheduling algorithm 比較。若要把它擴成更一般化或更耐受錯誤輸入的工具，最優先需要補上 `n_proc` / `quantum` / burst bounds validation、`gantt[]` / RR queue 邊界檢查、PID file 指向驗證，以及 benchmark parsing contract 的更明確錯誤處理。
