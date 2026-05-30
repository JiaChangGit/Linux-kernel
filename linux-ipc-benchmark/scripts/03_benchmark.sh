#!/usr/bin/env bash
# scripts/03_benchmark.sh - 執行 throughput benchmark 並顯示摘要。
#
# 用法：
#   sudo bash scripts/03_benchmark.sh            # 使用預設 200000 筆訊息
#   sudo bash scripts/03_benchmark.sh 500000     # 指定訊息數
set -euo pipefail

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'
BOLD='\033[1m'; NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
COUNT=${1:-200000}

# ── 前置檢查：確認權限、device 與 benchmark binary 都存在 ────────
[[ $EUID -ne 0 ]] && { echo "Run as root"; exit 1; }
for dev in /dev/mq_ipc /dev/shm_ipc; do
    [[ -c $dev ]] || { echo "Missing $dev — run 01_setup.sh first"; exit 1; }
done
BIN="${PROJECT_DIR}/user/benchmark"
[[ -x $BIN ]] || { echo "benchmark binary missing — run 01_setup.sh"; exit 1; }

echo ""
echo -e "${BOLD}${CYAN}════════════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}${CYAN}  linux-ipc-benchmark  —  Performance Comparison${NC}"
echo -e "${BOLD}${CYAN}════════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "  ${YELLOW}Message count  :${NC} ${COUNT}"
echo -e "  ${YELLOW}Message size   :${NC} 64 bytes"
echo -e "  ${YELLOW}Data volume    :${NC} $(echo "scale=1; ${COUNT} * 64 / 1048576" | bc) MB"
echo -e "  ${YELLOW}Ring capacity  :${NC} 512 slots"
echo -e "  ${YELLOW}Thread model   :${NC} 2 threads per test (producer + consumer)"
echo -e "  ${YELLOW}Sync barrier   :${NC} pthread_barrier (both threads start simultaneously)"
echo ""
echo "  Tests:"
echo "    [1] MQ  kfifo + blocking write/read  (2× copy per message)"
echo "    [2] SHM ring  + spinlock write/read  (2× copy per message)"
echo "    [3] SHM ring  + mmap  direct access  (ZERO copy per message)"
echo ""
echo -e "${CYAN}────────────────────────────────────────────────────────────────${NC}"
echo "  Running benchmark…  (this takes ~10–30 seconds)"
echo ""

# benchmark 本身會列印三條 IPC 路徑的測試結果。
"$BIN" "$COUNT"

echo ""
echo -e "${CYAN}════════════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}  Interpretation guide${NC}"
echo -e "${CYAN}════════════════════════════════════════════════════════════════${NC}"
cat <<'EOF'

  [1] vs [2]  —  Same copy count, different queue mechanism
      Difference shows kfifo wait-queue overhead vs spinlock.
      SHM-syscall is usually slightly faster because it avoids
      context-switch on a heavily loaded queue.

  [2] vs [3]  —  Same ring structure, syscall vs mmap
      This is the key comparison.  [3] eliminates:
        • copy_from_user / copy_to_user  (CPU time + cache pollution)
        • syscall overhead per message   (mode-switch cost)
      Expected speedup: 3×–10× depending on hardware.

  When to choose each:
    Message Queue  → decoupled producers/consumers, easy backpressure,
                     kernel manages lifetime, moderate throughput.
    Shared Memory  → ultra-high throughput, low latency, tight coupling
                     acceptable, explicit sync required.

EOF

echo -e "${GREEN}Benchmark complete.${NC}  Run  sudo bash scripts/04_cleanup.sh  when done."
echo ""
