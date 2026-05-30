#!/usr/bin/env bash
# =============================================================================
# 03_demo.sh
#
# 透過 SSH 連到正在執行的 QEMU VM，使用 6 個行程的 demo workload
# 執行所有排程演算法。輸出會顯示在 Host 終端機，也會保存到
# results/demo_output.txt。
# =============================================================================

set -euo pipefail

SSH_PORT="2222"
VM_USER="scheduler"
VM_PASS="scheduler123"
RESULTS_DIR="./results"
OUTPUT_FILE="${RESULTS_DIR}/demo_output.txt"

RED='\033[0;31m'; GREEN='\033[0;32m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
success() { echo -e "${GREEN}[OK]${NC}    $*"; }
die()     { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }
header()  {
    echo -e "\n${BOLD}${BLUE}============================================================${NC}"
    echo -e "${BOLD}${BLUE}  $*${NC}"
    echo -e "${BOLD}${BLUE}============================================================${NC}"
}

command -v sshpass &>/dev/null || die "sshpass is required. Re-run scripts/01_setup_env.sh."

SSH_OPTS=(
    -o StrictHostKeyChecking=no
    -o UserKnownHostsFile=/dev/null
    -o LogLevel=ERROR
    -o PreferredAuthentications=password
    -o PubkeyAuthentication=no
    -p "${SSH_PORT}"
)

# 在 VM 內執行指定命令。呼叫端負責傳入完整 shell command。
vm_run() {
    sshpass -p "${VM_PASS}" ssh "${SSH_OPTS[@]}" "${VM_USER}@localhost" "$@"
}

vm_run "test -x /home/${VM_USER}/scheduler" \
    || die "Scheduler binary not found in VM. Run scripts/02_start_vm.sh first."

mkdir -p "$RESULTS_DIR"
: > "$OUTPUT_FILE"

log() {
    echo -e "$@" | tee -a "$OUTPUT_FILE"
}

header "Demo Workload (6 Processes)"
log ""
log "  PID  Arrival  Burst  Priority"
log "  ---  -------  -----  --------"
log "   1      0       8       3"
log "   2      1       4       1     # priority 最高"
log "   3      2       9       4"
log "   4      3       5       2"
log "   5      4       2       5"
log "   6      5       1       3"
log ""

run_algo() {
    local algo="$1"
    local label="$2"
    local args="${3:-}"

    header "$label"
    vm_run "/home/${VM_USER}/scheduler ${algo} ${args} < /home/${VM_USER}/workload_demo.txt" \
        | tee -a "$OUTPUT_FILE"
    echo ""
}

run_algo "fcfs" "1. First-Come First-Served (FCFS)"
run_algo "sjf" "2. Shortest Job First - Non-Preemptive (SJF)"
run_algo "srtf" "3. Shortest Remaining Time First - Preemptive (SRTF)"
run_algo "priority" "4. Priority Scheduling - Non-Preemptive"

header "5. Round Robin - Time Quantum Comparison"
log ""
log "  Time Quantum (Q) 會影響 Round Robin 的排程行為："
log "  - Q 較小：通常 response time 較低，但切換次數較多"
log "  - Q 較大：行為會逐漸接近 FCFS"
log ""

for q in 1 2 4 8; do
    header "   Round Robin  Q = ${q}"
    vm_run "/home/${VM_USER}/scheduler rr ${q} < /home/${VM_USER}/workload_demo.txt" \
        | tee -a "$OUTPUT_FILE"
    echo "" | tee -a "$OUTPUT_FILE"
done

echo ""
success "Demo complete. Full output saved to: ${OUTPUT_FILE}"
echo ""
echo "  Next step: bash scripts/04_benchmark.sh"
echo ""
