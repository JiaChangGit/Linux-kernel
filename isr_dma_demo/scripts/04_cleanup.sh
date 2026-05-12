#!/usr/bin/env bash
# =============================================================================
#  04_cleanup.sh — Environment Cleanup
#  ISR + DMA Ring Buffer Demo | Ubuntu 24.04
# =============================================================================
#
#  Removes the kernel module, device node, and build artefacts.
#
#  Usage:  sudo bash scripts/04_cleanup.sh
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }

if [[ $EUID -ne 0 ]]; then
    echo -e "${RED}[ERROR]${RESET} Run as root: sudo bash scripts/04_cleanup.sh" >&2
    exit 1
fi

echo -e "${BOLD}"
cat <<'BANNER'
  ╔══════════════════════════════════════════════════════╗
  ║   ISR + DMA Ring Buffer — Cleanup                   ║
  ╚══════════════════════════════════════════════════════╝
BANNER
echo -e "${RESET}"

# ── 1. Unload kernel module ───────────────────────────────────────────────────
info "Unloading kernel module..."
if lsmod | grep -q isr_dma_module; then
    rmmod isr_dma_module && success "Module unloaded."
else
    warn "Module was not loaded — skipping."
fi

# ── 2. Remove device node (if manually created) ───────────────────────────────
info "Removing device node..."
if [[ -c /dev/isr_dma ]]; then
    rm -f /dev/isr_dma && success "/dev/isr_dma removed."
else
    warn "/dev/isr_dma not found — skipping."
fi

# ── 3. Clean build artefacts ──────────────────────────────────────────────────
info "Cleaning build artefacts..."

cd "$ROOT_DIR/kernel"
make clean 2>/dev/null && success "Kernel build artefacts cleaned."

cd "$ROOT_DIR/userspace"
make clean 2>/dev/null && success "Userspace build artefacts cleaned."

# Remove result files
rm -f "$ROOT_DIR/userspace/bench_results.txt"

# ── 4. Verify module is gone ──────────────────────────────────────────────────
info "Verifying cleanup..."
if ! lsmod | grep -q isr_dma_module; then
    success "Module not present in lsmod — clean."
fi
if [[ ! -c /dev/isr_dma ]]; then
    success "/dev/isr_dma gone — clean."
fi

# ── 5. Final dmesg check ──────────────────────────────────────────────────────
echo ""
echo -e "${CYAN}Last isr_dma kernel messages:${RESET}"
dmesg | grep isr_dma | tail -5 | sed 's/^/  /' || echo "  (none)"

echo ""
echo -e "${GREEN}${BOLD}════════════════════════════════════════════════${RESET}"
echo -e "${GREEN}${BOLD}  Cleanup complete. System restored.${RESET}"
echo -e "${GREEN}${BOLD}════════════════════════════════════════════════${RESET}"
