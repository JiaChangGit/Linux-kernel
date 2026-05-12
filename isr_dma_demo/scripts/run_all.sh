#!/usr/bin/env bash
# =============================================================================
#  run_all.sh — One-shot full demo runner
#  ISR + DMA Ring Buffer Demo | Ubuntu 24.04
# =============================================================================
#
#  Runs all four scripts in sequence:
#    01_setup.sh     → install deps, build module & consumer
#    02_demo.sh      → load module, live ISR demo
#    03_benchmark.sh → performance comparison
#    04_cleanup.sh   → unload module, clean artefacts
#
#  Usage:  sudo bash scripts/run_all.sh [bench_duration_sec]
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DURATION="${1:-5}"

if [[ $EUID -ne 0 ]]; then
    echo "[ERROR] run_all.sh must be run as root (sudo)." >&2
    exit 1
fi

echo "=========================================="
echo "  ISR + DMA Ring Buffer — Full Demo Run"
echo "=========================================="
echo ""

# Step 1: Setup (does not require root for apt, but insmod does)
echo ">>> 01_setup.sh"
bash "$SCRIPT_DIR/01_setup.sh"
echo ""

# Step 2: Demo
echo ">>> 02_demo.sh"
bash "$SCRIPT_DIR/02_demo.sh"
echo ""

# Step 3: Benchmark
echo ">>> 03_benchmark.sh $DURATION"
bash "$SCRIPT_DIR/03_benchmark.sh" "$DURATION"
echo ""

# Step 4: Cleanup
echo ">>> 04_cleanup.sh"
bash "$SCRIPT_DIR/04_cleanup.sh"

echo ""
echo "=========================================="
echo "  All done!"
echo "=========================================="
