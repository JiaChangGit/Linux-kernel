# =============================================================================
# CPU Scheduling QEMU Demo
#
# 常用 target：
#   make build       在 Host 編譯 scheduler.c，先確認 C 程式可建置
#   make demo-host   在 Host 直接跑 demo workload，不需要 QEMU
#   make setup       建立 QEMU disk 與 cloud-init seed
#   make start       啟動 QEMU VM，並等待 scheduler 在 VM 內準備完成
#   make demo        在 VM 內跑 6 個行程的 demo workload
#   make bench       在 VM 內跑 12 個行程的 benchmark workload
#   make clean       關閉 VM，移除本次產生的 VM 檔案
#   make clean-full  額外刪除下載的 Ubuntu base image
#   make all         依序執行 setup -> start -> demo -> bench
# =============================================================================

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
SRC     = src/scheduler.c
BIN     = /tmp/scheduler_host

WORKLOAD_DEMO  = src/workload_demo.txt
WORKLOAD_BENCH = src/workload_bench.txt

.PHONY: build demo-host setup start demo bench clean clean-full all

build:
	@echo "Compiling scheduler.c..."
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)
	@echo "Binary: $(BIN)"

demo-host: build
	@echo ""
	@echo "=== HOST-SIDE DEMO (no QEMU) ==="
	@echo ""
	@for algo in fcfs sjf srtf priority; do \
		echo "--- $$algo ---"; \
		$(BIN) $$algo < $(WORKLOAD_DEMO); \
		echo ""; \
	done
	@for q in 1 2 4 8; do \
		echo "--- rr Q=$$q ---"; \
		$(BIN) rr $$q < $(WORKLOAD_DEMO); \
		echo ""; \
	done

setup:
	bash scripts/01_setup_env.sh

start:
	bash scripts/02_start_vm.sh

demo:
	bash scripts/03_demo.sh

bench:
	bash scripts/04_benchmark.sh

clean:
	bash scripts/05_cleanup.sh

clean-full:
	bash scripts/05_cleanup.sh --full

all: setup start demo bench
