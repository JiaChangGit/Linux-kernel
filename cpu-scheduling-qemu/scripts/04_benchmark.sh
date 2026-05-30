#!/usr/bin/env bash
# =============================================================================
# 04_benchmark.sh
#
# 在 QEMU VM 內使用 12 個行程的 benchmark workload 執行所有排程演算法，
# 解析 scheduler 輸出的 BENCHMARK line，產生 CSV 與文字報告。
#
# 指標：
#   AWT：Average Waiting Time
#   ATT：Average Turnaround Time
#   ART：Average Response Time
# =============================================================================

set -euo pipefail

SSH_PORT="2222"
VM_USER="scheduler"
VM_PASS="scheduler123"
RESULTS_DIR="./results"
CSV_FILE="${RESULTS_DIR}/benchmark.csv"
REPORT_FILE="${RESULTS_DIR}/benchmark_report.txt"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; NC='\033[0m'

info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
success() { echo -e "${GREEN}[OK]${NC}    $*"; }
die()     { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

command -v sshpass &>/dev/null || die "sshpass is required. Re-run scripts/01_setup_env.sh."
command -v bc &>/dev/null || die "bc is required. Install it with: sudo apt install -y bc"

SSH_OPTS=(
    -o StrictHostKeyChecking=no
    -o UserKnownHostsFile=/dev/null
    -o LogLevel=ERROR
    -o PreferredAuthentications=password
    -o PubkeyAuthentication=no
    -p "${SSH_PORT}"
)

vm_run() {
    sshpass -p "${VM_PASS}" ssh "${SSH_OPTS[@]}" "${VM_USER}@localhost" "$@"
}

vm_run "test -x /home/${VM_USER}/scheduler" \
    || die "Scheduler binary not found in VM. Run scripts/02_start_vm.sh first."

mkdir -p "$RESULTS_DIR"
echo "Algorithm,AWT,ATT,ART" > "$CSV_FILE"

# run_bench <algorithm> [quantum]
# 執行 VM 內 scheduler，抽出固定格式的 BENCHMARK line 後寫入 CSV。
run_bench() {
    local algo="$1"
    local quantum="${2:-}"
    local args="${algo}${quantum:+ $quantum}"
    local raw bench_line

    raw=$(vm_run "/home/${VM_USER}/scheduler ${args} < /home/${VM_USER}/workload_bench.txt" 2>&1)

    if ! bench_line=$(echo "$raw" | grep '^BENCHMARK'); then
        echo "$raw" >&2
        die "Scheduler output did not contain a BENCHMARK line for: ${args}"
    fi

    local label awt att art
    label=$(echo "$bench_line" | awk '{print $2}')
    awt=$(echo "$bench_line" | grep -oP 'AWT=\K[\d.]+')
    att=$(echo "$bench_line" | grep -oP 'ATT=\K[\d.]+')
    art=$(echo "$bench_line" | grep -oP 'ART=\K[\d.]+')

    echo "${label},${awt},${att},${art}" >> "$CSV_FILE"
    printf "%s %s %s %s" "$label" "$awt" "$att" "$art"
}

info "Running benchmark workload (12 processes) against all algorithms..."
echo ""

declare -a ROWS

collect() {
    local label="$1"
    shift

    local result
    result=$(run_bench "$@")
    ROWS+=("$result")
    info "  Completed: $label"
}

collect "FCFS"                       fcfs
collect "SJF (Non-Preemptive)"       sjf
collect "SRTF (Preemptive)"          srtf
collect "Priority (Non-Preemptive)"  priority
collect "Round Robin Q=1"            rr 1
collect "Round Robin Q=2"            rr 2
collect "Round Robin Q=4"            rr 4
collect "Round Robin Q=8"            rr 8

echo ""

print_table() {
    printf "+-------------------------------+----------+----------+----------+\n"
    printf "| %-29s | %8s | %8s | %8s |\n" "Algorithm" "Avg WT" "Avg TAT" "Avg RT"
    printf "+-------------------------------+----------+----------+----------+\n"

    for row in "${ROWS[@]}"; do
        local lbl awt att art
        read -r lbl awt att art <<< "$row"
        lbl="${lbl//_/ }"
        printf "| %-29s | %8.2f | %8.2f | %8.2f |\n" "$lbl" "$awt" "$att" "$art"
    done

    printf "+-------------------------------+----------+----------+----------+\n"
}

{
    echo ""
    echo "============================================================"
    echo "  CPU Scheduling Algorithm Benchmark Results"
    echo "  Workload: 12 processes | Platform: QEMU Ubuntu 24.04 x86_64"
    echo "============================================================"
    echo ""
    echo "  Columns:"
    echo "    Avg WT  = Average Waiting Time   (lower is better)"
    echo "    Avg TAT = Average Turnaround Time (lower is better)"
    echo "    Avg RT  = Average Response Time   (lower is better)"
    echo ""
    print_table
    echo ""

    echo "  Analysis:"

    best_awt_row=""
    best_awt=99999
    for row in "${ROWS[@]}"; do
        read -r lbl awt _ _ <<< "$row"
        is_less=$(echo "$awt < $best_awt" | bc -l)
        if [[ "$is_less" == "1" ]]; then
            best_awt="$awt"
            best_awt_row="${lbl//_/ }"
        fi
    done
    echo "    Best Average Waiting Time    = ${best_awt_row} (${best_awt})"

    best_att_row=""
    best_att=99999
    for row in "${ROWS[@]}"; do
        read -r lbl _ att _ <<< "$row"
        is_less=$(echo "$att < $best_att" | bc -l)
        if [[ "$is_less" == "1" ]]; then
            best_att="$att"
            best_att_row="${lbl//_/ }"
        fi
    done
    echo "    Best Average Turnaround Time = ${best_att_row} (${best_att})"

    best_art_row=""
    best_art=99999
    for row in "${ROWS[@]}"; do
        read -r lbl _ _ art <<< "$row"
        is_less=$(echo "$art < $best_art" | bc -l)
        if [[ "$is_less" == "1" ]]; then
            best_art="$art"
            best_art_row="${lbl//_/ }"
        fi
    done
    echo "    Best Average Response Time   = ${best_art_row} (${best_art})"

    echo ""
    echo "  Round Robin Time Quantum Effect:"
    echo "    As Q increases, RR converges toward FCFS behaviour."
    echo "    Smaller Q usually improves first response time."
    echo "    A very small Q can increase total waiting/turnaround time."
    echo ""
    echo "============================================================"
    echo ""
} | tee "$REPORT_FILE"

success "CSV data saved to   : ${CSV_FILE}"
success "Text report saved to: ${REPORT_FILE}"
echo ""
echo "  Next step: bash scripts/05_cleanup.sh   (when finished)"
echo ""
