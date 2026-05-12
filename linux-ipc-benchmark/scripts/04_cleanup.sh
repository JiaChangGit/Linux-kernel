#!/usr/bin/env bash
# scripts/04_cleanup.sh  —  unload modules, remove build artifacts
set -euo pipefail

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
ok()   { echo -e "${GREEN}[OK]${NC}  $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

[[ $EUID -ne 0 ]] && { echo "Run as root"; exit 1; }

echo "================================================================"
echo "  linux-ipc-benchmark  —  Cleanup"
echo "================================================================"
echo ""

# ── 1. Unload modules ──────────────────────────────────────────────
echo "[1/3]  Unloading kernel modules…"
if lsmod | grep -q shm_module; then
    rmmod shm_module && ok "shm_module removed"
else
    warn "shm_module not loaded — skipping"
fi
if lsmod | grep -q mq_module; then
    rmmod mq_module  && ok "mq_module removed"
else
    warn "mq_module not loaded — skipping"
fi

# ── 2. Clean build artifacts ───────────────────────────────────────
echo ""
echo "[2/3]  Removing build artifacts…"
make -C "${PROJECT_DIR}" clean 2>&1 | grep -v "^make" | head -20 || true
ok "Build artifacts removed"

# ── 3. Verify ──────────────────────────────────────────────────────
echo ""
echo "[3/3]  Verification…"
PASS=1

for mod in mq_module shm_module; do
    if lsmod | grep -q "$mod"; then
        warn "$mod still loaded!"
        PASS=0
    else
        ok "$mod  not in lsmod ✓"
    fi
done

for dev in /dev/mq_ipc /dev/shm_ipc; do
    if [[ -e $dev ]]; then
        warn "$dev  still present!"
        PASS=0
    else
        ok "$dev  removed ✓"
    fi
done

for proc in /proc/mq_stats /proc/shm_stats; do
    if [[ -e $proc ]]; then
        warn "$proc  still present!"
        PASS=0
    else
        ok "$proc  removed ✓"
    fi
done

echo ""
if [[ $PASS -eq 1 ]]; then
    echo -e "${GREEN}Cleanup complete — environment fully restored.${NC}"
else
    echo -e "${YELLOW}Cleanup finished with warnings. Check dmesg for details.${NC}"
fi
echo "================================================================"
