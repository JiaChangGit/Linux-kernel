#!/usr/bin/env bash
# scripts/01_setup.sh - 安裝依賴、建置程式、載入 kernel modules。
set -euo pipefail

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
ok()   { echo -e "${GREEN}[OK]${NC}  $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
die()  { echo -e "${RED}[ERR]${NC} $*"; exit 1; }

echo "================================================================"
echo "  linux-ipc-benchmark  —  Environment Setup"
echo "================================================================"

# ── 0. 權限檢查：載入 module 與修改 /dev 權限需要 root ────────────
[[ $EUID -ne 0 ]] && die "Run as root:  sudo bash scripts/01_setup.sh"

# ── 1. 顯示環境資訊：確認 kernel headers 要對應目前核心版本 ──────
KVER=$(uname -r)
echo "  Kernel  : $KVER"
echo "  OS      : $(lsb_release -ds 2>/dev/null || cat /etc/os-release | grep PRETTY | cut -d= -f2 | tr -d '"')"
echo ""

# ── 2. 安裝建置依賴：kernel module 需要 headers 與 kmod ──────────
echo "[1/4]  Installing build dependencies…"
apt-get update -qq
apt-get install -y -qq \
    build-essential \
    "linux-headers-${KVER}" \
    kmod \
    2>&1 | tail -3
ok "Dependencies installed"

# ── 3. 建置 kernel modules：產生 mq_module.ko 與 shm_module.ko ───
echo ""
echo "[2/4]  Building kernel modules…"
cd kernel && make
cd ..
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

make -C "${PROJECT_DIR}" kernel 2>&1 | grep -E "^  (CC|LD|Building|make)" || true
ls -lh "${PROJECT_DIR}"/kernel/*.ko
ok "mq_module.ko  and  shm_module.ko  built"

# ── 4. 建置 user-space 工具：demo 與 benchmark ───────────────────
echo ""
echo "[3/4]  Building userspace programs…"
make -C "${PROJECT_DIR}" user  2>&1 | grep -v "^make" | head -10
ls -lh "${PROJECT_DIR}"/user/mq_demo \
        "${PROJECT_DIR}"/user/shm_demo \
        "${PROJECT_DIR}"/user/benchmark
ok "mq_demo  shm_demo  benchmark  built"

# ── 5. 載入 modules：若已載入就略過，避免重複 insmod ───────────
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

# 等 udev 建立 /dev 節點後，放寬權限讓一般測試程式可開啟裝置。
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
