# =============================================================================
# Makefile — CPU Scheduling QEMU Demo
#
# Targets:
#   make build       — compile scheduler.c on the host (sanity check)
#   make demo-host   — run all algorithms locally on the host (no QEMU needed)
#   make setup       — run 01_setup_env.sh
#   make start       — run 02_start_vm.sh
#   make demo        — run 03_demo.sh (requires running VM)
#   make bench       — run 04_benchmark.sh (requires running VM)
#   make clean       — run 05_cleanup.sh (stop VM, remove disk)
#   make clean-full  — same, also remove base image
#   make all         — setup → start → demo → bench (full pipeline)
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
