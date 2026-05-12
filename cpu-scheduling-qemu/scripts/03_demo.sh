#!/usr/bin/env bash
# =============================================================================
# 03_demo.sh — Run all scheduling algorithms inside the QEMU VM
#
# Connects to the running VM over SSH and executes each algorithm against
# the demo workload (6 processes).  Output is displayed on the host terminal
# and simultaneously saved to results/demo_output.txt.
#
# Algorithms demonstrated:
#   FCFS | SJF Non-preemptive | SRTF (Preemptive SJF) |
#   Priority Non-preemptive | Round Robin (Q=1, Q=2, Q=4, Q=8)
# =============================================================================

set -euo pipefail

SSH_PORT="2222"
VM_USER="scheduler"
VM_PASS="scheduler123"
RESULTS_DIR="./results"
OUTPUT_FILE="${RESULTS_DIR}/demo_output.txt"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
success() { echo -e "${GREEN}[OK]${NC}    $*"; }
header()  { echo -e "\n${BOLD}${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"; \
            echo -e "${BOLD}${BLUE}  $*${NC}"; \
            echo -e "${BOLD}${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"; }
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

# ── SSH helper: run a command inside the VM ────────────────────────────────────
vm_run() {
    sshpass -p "${VM_PASS}" ssh "${SSH_OPTS[@]}" "${VM_USER}@localhost" "$@"
}

# ── Pre-flight ────────────────────────────────────────────────────────────────
vm_run "test -x /home/${VM_USER}/scheduler" \
    || die "Scheduler binary not found in VM. Check that cloud-init completed."

mkdir -p "$RESULTS_DIR"
: > "$OUTPUT_FILE"    # truncate / create output file

log() {
    # Print to terminal AND append to output file
    echo -e "$@" | tee -a "$OUTPUT_FILE"
}

# ── Print demo workload ───────────────────────────────────────────────────────
header "Demo Workload (6 Processes)"
log ""
log "  PID  Arrival  Burst  Priority"
log "  ---  -------  -----  --------"
log "   1      0       8       3"
log "   2      1       4       1     ← highest priority"
log "   3      2       9       4"
log "   4      3       5       2"
log "   5      4       2       5"
log "   6      5       1       3"
log ""

run_algo() {
    local algo="$1"
    local label="$2"
    local args="${3:-}"     # optional extra arg (e.g. time quantum)

    header "$label"
    vm_run "/home/${VM_USER}/scheduler ${algo} ${args} < /home/${VM_USER}/workload_demo.txt" \
        | tee -a "$OUTPUT_FILE"
    echo ""
}

# ── FCFS ──────────────────────────────────────────────────────────────────────
run_algo "fcfs" "1. First-Come First-Served (FCFS)"

# ── SJF Non-preemptive ────────────────────────────────────────────────────────
run_algo "sjf" "2. Shortest Job First — Non-Preemptive (SJF)"

# ── SRTF Preemptive ───────────────────────────────────────────────────────────
run_algo "srtf" "3. Shortest Remaining Time First — Preemptive (SRTF)"

# ── Priority Non-preemptive ───────────────────────────────────────────────────
run_algo "priority" "4. Priority Scheduling — Non-Preemptive"

# ── Round Robin — varying time quantum ────────────────────────────────────────
header "5. Round Robin — Time Quantum Comparison"
log ""
log "  Demonstrating how Time Quantum (Q) affects scheduling behaviour:"
log "  Small Q → lower response time, higher context-switch overhead"
log "  Large Q → approaches FCFS behaviour"
log ""

for q in 1 2 4 8; do
    header "   Round Robin  Q = ${q}"
    vm_run "/home/${VM_USER}/scheduler rr ${q} < /home/${VM_USER}/workload_demo.txt" \
        | tee -a "$OUTPUT_FILE"
    echo "" | tee -a "$OUTPUT_FILE"
done

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
success "Demo complete. Full output saved to: ${OUTPUT_FILE}"
echo ""
echo "  Next step: bash scripts/04_benchmark.sh"
echo ""
