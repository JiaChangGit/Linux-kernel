#!/usr/bin/env bash
# =============================================================================
#  03_benchmark.sh — Performance Comparison: ISR+DMA vs Naive
#  ISR + DMA Ring Buffer Demo | Ubuntu 24.04
# =============================================================================
#
#  Runs the userspace benchmark (consumer binary) and then renders
#  a formatted comparison table plus an ASCII bar chart from the results.
#
#  Usage:  sudo bash scripts/03_benchmark.sh [duration_sec]
#          duration_sec: seconds per test run (default: 5)
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BLUE='\033[0;34m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
error()   { echo -e "${RED}[ERROR]${RESET} $*" >&2; exit 1; }

DURATION="${1:-5}"

# ── Root check ────────────────────────────────────────────────────────────────
if [[ $EUID -ne 0 ]]; then
    error "Run as root: sudo bash scripts/03_benchmark.sh"
fi

# ── Pre-flight ────────────────────────────────────────────────────────────────
[[ -f "$ROOT_DIR/userspace/consumer" ]] || \
    error "Consumer not built. Run: bash scripts/01_setup.sh"

if ! lsmod | grep -q isr_dma_module; then
    info "Module not loaded — loading now..."
    insmod "$ROOT_DIR/kernel/isr_dma_module.ko"
    sleep 1
fi

[[ -c /dev/isr_dma ]] || {
    MAJOR=$(grep isr_dma /proc/devices | awk '{print $1}')
    mknod /dev/isr_dma c "$MAJOR" 0
}
chmod 666 /dev/isr_dma

echo -e "${BOLD}"
cat <<'BANNER'
  ╔══════════════════════════════════════════════════════╗
  ║   ISR + DMA Ring Buffer — Benchmark                 ║
  ╚══════════════════════════════════════════════════════╝
BANNER
echo -e "${RESET}"

info "Running benchmark (${DURATION}s per path)..."
info "  Path A: mmap zero-copy  (DMA-coherent shared memory)"
info "  Path B: read() syscall  (kernel copy_to_user)"
echo ""

# ── Run consumer benchmark ────────────────────────────────────────────────────
cd "$ROOT_DIR/userspace"
"$ROOT_DIR/userspace/consumer" "$DURATION"

# ── Load results ──────────────────────────────────────────────────────────────
if [[ ! -f "$ROOT_DIR/userspace/bench_results.txt" ]]; then
    error "bench_results.txt not found — consumer may have failed."
fi

source "$ROOT_DIR/userspace/bench_results.txt"

# Safe defaults in case any variable is unset
mmap_lat_ns="${mmap_lat_ns:-1}"
mmap_ops="${mmap_ops:-0}"
read_lat_ns="${read_lat_ns:-1}"
read_ops="${read_ops:-0}"
speedup="${speedup:-0}"

# ── Kernel-side stats ─────────────────────────────────────────────────────────
echo ""
echo -e "${CYAN}${BOLD}════ Kernel-Side Stats (from /proc) ════${RESET}"
cat /proc/isr_dma_stats 2>/dev/null | sed 's/^/  /' || echo "  (not available)"

# ── Render comparison table ───────────────────────────────────────────────────
echo ""
echo -e "${GREEN}${BOLD}════ Benchmark Results ════${RESET}"
printf "\n"
printf "  %-32s  %14s  %12s\n" "Access Path" "Avg latency(ns)" "Total ops"
printf "  %s\n" "──────────────────────────────────────────────────────────────"
printf "  %-32s  %14s  %12s\n" \
    "A) ISR+DMA mmap (zero-copy)" \
    "${mmap_lat_ns}" \
    "${mmap_ops}"
printf "  %-32s  %14s  %12s\n" \
    "B) read() syscall (copy)" \
    "${read_lat_ns}" \
    "${read_ops}"
printf "\n"
printf "  Speedup (B ÷ A): ${GREEN}${BOLD}${speedup}×${RESET}\n"
printf "\n"

# ── ASCII Bar Chart ────────────────────────────────────────────────────────────
echo -e "${CYAN}${BOLD}════ ASCII Bar Chart (lower = faster) ════${RESET}"
echo ""

render_bar() {
    local label="$1"
    local value="$2"
    local max_val="$3"
    local width=50
    local color="$4"

    local filled=0
    if [[ "$max_val" -gt 0 ]]; then
        filled=$(( value * width / max_val ))
    fi
    [[ "$filled" -gt "$width" ]] && filled=$width

    printf "  %-28s [" "$label"
    printf "${color}"
    for ((i=0; i<filled; i++)); do printf "█"; done
    printf "${RESET}"
    for ((i=filled; i<width; i++)); do printf "░"; done
    printf "] %6s ns/op\n" "$value"
}

max_lat=$(( mmap_lat_ns > read_lat_ns ? mmap_lat_ns : read_lat_ns ))
[[ "$max_lat" -eq 0 ]] && max_lat=1

render_bar "A) mmap (zero-copy)"  "$mmap_lat_ns" "$max_lat" "${GREEN}"
render_bar "B) read() syscall"    "$read_lat_ns"  "$max_lat" "${YELLOW}"

echo ""

# ── Interpretation ────────────────────────────────────────────────────────────
echo -e "${CYAN}${BOLD}════ Interpretation ════${RESET}"
echo ""
cat <<INTERP
  Why is Path A (mmap/DMA) faster?
  ─────────────────────────────────
  • Path A avoids copy_to_user() entirely. The ISR writes directly into
    DMA-coherent memory that is already mapped into userspace — no kernel
    bounce buffer, no page fault, no TLB shootdown.

  • Path B (read syscall) crosses the kernel/user boundary on every slot:
    the kernel must validate the userspace pointer, call copy_to_user(),
    and return through the syscall path — adding hundreds of nanoseconds
    per operation.

  • In real embedded / real-time systems (network cards, audio DMA,
    frame grabbers), this difference translates to measurable latency
    reduction and lower CPU utilisation.

  Ring Buffer benefits over a plain buffer:
  ──────────────────────────────────────────
  • Lock-free producer/consumer (using atomic head/tail pointers).
  • ISR can write while consumer reads — no global lock needed.
  • Fixed memory footprint — no dynamic allocation in interrupt context.

INTERP

# ── Throughput estimate ────────────────────────────────────────────────────────
if [[ "$mmap_lat_ns" -gt 0 ]]; then
    mmap_mops=$(( 1000000000 / mmap_lat_ns ))
    echo "  Estimated max throughput:"
    echo "    mmap path : ~${mmap_mops} Mops/s (per-core)"
fi
if [[ "$read_lat_ns" -gt 0 ]]; then
    read_mops=$(( 1000000000 / read_lat_ns ))
    echo "    read path : ~${read_mops} Mops/s (per-core)"
fi

echo ""
echo -e "${GREEN}${BOLD}════════════════════════════════════════════════${RESET}"
echo -e "${GREEN}${BOLD}  Benchmark complete!${RESET}"
echo -e "  Next step: ${BOLD}sudo bash scripts/04_cleanup.sh${RESET}"
echo -e "${GREEN}${BOLD}════════════════════════════════════════════════${RESET}"
