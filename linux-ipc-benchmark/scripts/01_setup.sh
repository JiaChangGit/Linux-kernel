#!/usr/bin/env bash
# scripts/01_setup.sh  —  install deps, build modules + userspace, load modules
set -euo pipefail

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
ok()   { echo -e "${GREEN}[OK]${NC}  $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
die()  { echo -e "${RED}[ERR]${NC} $*"; exit 1; }

echo "================================================================"
echo "  linux-ipc-benchmark  —  Environment Setup"
echo "================================================================"

# ── 0. root check ──────────────────────────────────────────────────
[[ $EUID -ne 0 ]] && die "Run as root:  sudo bash scripts/01_setup.sh"

# ── 1. Ubuntu 24 sanity check ──────────────────────────────────────
KVER=$(uname -r)
echo "  Kernel  : $KVER"
echo "  OS      : $(lsb_release -ds 2>/dev/null || cat /etc/os-release | grep PRETTY | cut -d= -f2 | tr -d '"')"
echo ""

# ── 2. Install build dependencies ──────────────────────────────────
echo "[1/4]  Installing build dependencies…"
apt-get update -qq
apt-get install -y -qq \
    build-essential \
    "linux-headers-${KVER}" \
    kmod \
    2>&1 | tail -3
ok "Dependencies installed"

# ── 3. Build kernel modules ────────────────────────────────────────
echo ""
echo "[2/4]  Building kernel modules…"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

make -C "${PROJECT_DIR}" kernel 2>&1 | grep -E "^  (CC|LD|Building|make)" || true
ls -lh "${PROJECT_DIR}"/kernel/*.ko
ok "mq_module.ko  and  shm_module.ko  built"

# ── 4. Build userspace ─────────────────────────────────────────────
echo ""
echo "[3/4]  Building userspace programs…"
make -C "${PROJECT_DIR}" user  2>&1 | grep -v "^make" | head -10
ls -lh "${PROJECT_DIR}"/user/mq_demo \
        "${PROJECT_DIR}"/user/shm_demo \
        "${PROJECT_DIR}"/user/benchmark
ok "mq_demo  shm_demo  benchmark  built"

# ── 5. Load modules ────────────────────────────────────────────────
echo ""
echo "[4/4]  Loading kernel modules…"

if lsmod | grep -q mq_module; then
    warn "mq_module already loaded — skipping"
else
    insmod "${PROJECT_DIR}/kernel/mq_module.ko"
    ok "mq_module loaded"
fi

if lsmod | grep -q shm_module; then
    warn "shm_module already loaded — skipping"
else
    insmod "${PROJECT_DIR}/kernel/shm_module.ko"
    ok "shm_module loaded"
fi

# give udev a moment, then fix permissions
sleep 0.5
chmod 666 /dev/mq_ipc  /dev/shm_ipc

echo ""
echo "================================================================"
echo "  Loaded modules"
lsmod | grep -E "mq_module|shm_module"
echo ""
echo "  Character devices"
ls -lh /dev/mq_ipc /dev/shm_ipc
echo ""
echo "  /proc stats"
echo "    /proc/mq_stats  → $(cat /proc/mq_stats | wc -l) lines"
echo "    /proc/shm_stats → $(cat /proc/shm_stats | wc -l) lines"
echo ""
echo "  Kernel log (last 6 lines)"
dmesg | tail -6 | sed 's/^/    /'
echo ""
ok "Setup complete.  Run  sudo bash scripts/02_demo.sh  next."
echo "================================================================"
