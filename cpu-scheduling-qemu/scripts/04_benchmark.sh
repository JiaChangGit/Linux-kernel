#!/usr/bin/env bash
# =============================================================================
# 04_benchmark.sh — Performance comparison across all scheduling algorithms
#
# Runs each algorithm against the 12-process benchmark workload inside the VM,
# extracts Average Waiting Time (AWT), Average Turnaround Time (ATT), and
# Average Response Time (ART) from each run, then prints a formatted comparison
# table and writes results/benchmark.csv for further analysis.
#
# Round Robin is tested at Q=1, 2, 4, 8 to show the time-quantum effect.
# =============================================================================

set -euo pipefail

SSH_PORT="2222"
VM_USER="scheduler"
VM_PASS="scheduler123"
RESULTS_DIR="./results"
CSV_FILE="${RESULTS_DIR}/benchmark.csv"
REPORT_FILE="${RESULTS_DIR}/benchmark_report.txt"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
success() { echo -e "${GREEN}[OK]${NC}    $*"; }
die()     { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

command -v sshpass &>/dev/null || die "sshpass is required. Re-run scripts/01_setup_env.sh."

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

# ── Verify VM is reachable ────────────────────────────────────────────────────
vm_run "test -x /home/${VM_USER}/scheduler" \
    || die "Scheduler binary not found in VM. Run 02_start_vm.sh first."

mkdir -p "$RESULTS_DIR"

# ── CSV header ────────────────────────────────────────────────────────────────
echo "Algorithm,AWT,ATT,ART" > "$CSV_FILE"

# ── run_bench <algo_arg> [quantum] ────────────────────────────────────────────
# Executes the scheduler inside the VM, captures the BENCHMARK line, and
# writes a row to the CSV.
run_bench() {
    local algo="$1"
    local quantum="${2:-}"
    local args="${algo}${quantum:+ $quantum}"

    # Run and capture full output
    local raw
    raw=$(vm_run "/home/${VM_USER}/scheduler ${args} < /home/${VM_USER}/workload_bench.txt" 2>&1)

    # Extract the machine-readable BENCHMARK line:
    #   BENCHMARK <label> AWT=<f> ATT=<f> ART=<f>
    local bench_line
    bench_line=$(echo "$raw" | grep '^BENCHMARK')

    local label awt att art
    label=$(echo "$bench_line" | awk '{print $2}')
    awt=$(echo   "$bench_line" | grep -oP 'AWT=\K[\d.]+')
    att=$(echo   "$bench_line" | grep -oP 'ATT=\K[\d.]+')
    art=$(echo   "$bench_line" | grep -oP 'ART=\K[\d.]+')

    echo "${label},${awt},${att},${art}" >> "$CSV_FILE"

    # Return parsed values for the summary table
    printf "%s %s %s %s" "$label" "$awt" "$att" "$art"
}

# ── Run all algorithms ────────────────────────────────────────────────────────
info "Running benchmark workload (12 processes) against all algorithms..."
echo ""

declare -a ROWS
declare -a LABELS

collect() {
    local label="$1"; shift
    local result
    result=$(run_bench "$@")
    ROWS+=("$result")
    info "  Completed: $label"
}

collect "FCFS"                    fcfs
collect "SJF (Non-Preemptive)"    sjf
collect "SRTF (Preemptive)"       srtf
collect "Priority (Non-Preemptive)" priority
collect "Round Robin Q=1"         rr 1
collect "Round Robin Q=2"         rr 2
collect "Round Robin Q=4"         rr 4
collect "Round Robin Q=8"         rr 8

echo ""

# ── Print formatted comparison table ─────────────────────────────────────────
print_table() {
    local border="╔═══════════════════════════════╦══════════╦══════════╦══════════╗"
    local header="║ Algorithm                     ║  Avg WT  ║  Avg TAT ║  Avg RT  ║"
    local divider="╠═══════════════════════════════╬══════════╬══════════╬══════════╣"
    local footer="╚═══════════════════════════════╩══════════╩══════════╩══════════╝"

    echo "$border"
    echo "$header"
    echo "$divider"

    for row in "${ROWS[@]}"; do
        local lbl awt att art
        read -r lbl awt att art <<< "$row"

        # Replace underscores with spaces for readability
        lbl="${lbl//_/ }"
        printf "║ %-29s ║ %8.2f ║ %8.2f ║ %8.2f ║\n" \
               "$lbl" "$awt" "$att" "$art"
    done

    echo "$footer"
}

{
    echo ""
    echo "════════════════════════════════════════════════════════════════════"
    echo "  CPU Scheduling Algorithm Benchmark Results"
    echo "  Workload: 12 processes | Platform: QEMU Ubuntu 24.04 x86_64"
    echo "════════════════════════════════════════════════════════════════════"
    echo ""
    echo "  Columns:"
    echo "    Avg WT  — Average Waiting Time   (lower is better)"
    echo "    Avg TAT — Average Turnaround Time (lower is better)"
    echo "    Avg RT  — Average Response Time   (lower is better)"
    echo ""
    print_table
    echo ""

    # ── Find best AWT ────────────────────────────────────────────────────────
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
    echo "    Best Average Waiting Time    → ${best_awt_row} (${best_awt})"

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
    echo "    Best Average Turnaround Time → ${best_att_row} (${best_att})"

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
    echo "    Best Average Response Time   → ${best_art_row} (${best_art})"

    echo ""
    echo "  Round Robin Time Quantum Effect:"
    echo "    As Q increases, RR converges toward FCFS behaviour."
    echo "    Smaller Q gives lower response time but increases context-switch overhead."
    echo "    Optimal Q depends on the average burst time of the workload."
    echo ""
    echo "════════════════════════════════════════════════════════════════════"
    echo ""
} | tee "$REPORT_FILE"

success "CSV data saved to   : ${CSV_FILE}"
success "Text report saved to: ${REPORT_FILE}"
echo ""
echo "  Next step: bash scripts/05_cleanup.sh   (when finished)"
echo ""
