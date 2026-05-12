#!/usr/bin/env bash
# =============================================================================
# 05_cleanup.sh — Stop the QEMU VM and remove generated VM artifacts
#
# What gets removed:
#   ./vm/qemu.pid          — PID file
#   ./vm/qemu.log          — QEMU console log
#   ./vm/seed.iso          — cloud-init seed ISO
#   ./vm/user-data         — cloud-init user-data (contains generated content)
#   ./vm/meta-data         — cloud-init meta-data
#   ./vm/ubuntu2404.qcow2  — VM disk image
#   ./vm/ubuntu2404-base.img — downloaded base image (optional, see --full)
#
# The ./results/ directory is intentionally KEPT so benchmark data persists.
#
# Usage:
#   bash scripts/05_cleanup.sh          # stop VM, remove disk, keep base image
#   bash scripts/05_cleanup.sh --full   # also remove the downloaded base image
# =============================================================================

set -euo pipefail

VM_DIR="./vm"
PID_FILE="${VM_DIR}/qemu.pid"
FULL_CLEAN=false

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; NC='\033[0m'

info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
success() { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }

[[ "${1:-}" == "--full" ]] && FULL_CLEAN=true

# ── Step 1: Gracefully shut down the VM ──────────────────────────────────────
if [[ -f "$PID_FILE" ]]; then
    QEMU_PID=$(cat "$PID_FILE")
    if kill -0 "$QEMU_PID" 2>/dev/null; then
        info "Sending ACPI power-off to VM (PID ${QEMU_PID})..."
        # Try graceful shutdown via QEMU monitor signal first
        kill -TERM "$QEMU_PID" 2>/dev/null || true
        sleep 3
        # Force-kill if still running
        if kill -0 "$QEMU_PID" 2>/dev/null; then
            warn "VM did not exit gracefully — sending SIGKILL."
            kill -KILL "$QEMU_PID" 2>/dev/null || true
        fi
        success "QEMU process stopped."
    else
        warn "PID file exists but process ${QEMU_PID} is not running."
    fi
    rm -f "$PID_FILE"
else
    info "No PID file found — VM may already be stopped."
fi

# ── Step 2: Remove generated VM files ────────────────────────────────────────
remove_if_exists() {
    local path="$1"
    if [[ -f "$path" ]]; then
        rm -f "$path"
        info "Removed: $path"
    fi
}

remove_if_exists "${VM_DIR}/seed.iso"
remove_if_exists "${VM_DIR}/user-data"
remove_if_exists "${VM_DIR}/meta-data"
remove_if_exists "${VM_DIR}/ubuntu2404.qcow2"
remove_if_exists "${VM_DIR}/qemu.log"
remove_if_exists "${VM_DIR}/qemu-serial.log"

if $FULL_CLEAN; then
    remove_if_exists "${VM_DIR}/ubuntu2404-base.img"
    info "Full clean: base image also removed."
fi

# ── Step 3: Remove empty vm/ directory if nothing left ───────────────────────
if [[ -d "$VM_DIR" ]] && [[ -z "$(ls -A "$VM_DIR" 2>/dev/null)" ]]; then
    rmdir "$VM_DIR"
    info "Removed empty directory: ${VM_DIR}/"
fi

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
success "Cleanup complete."
echo ""
echo "  Retained: ./results/   (demo output and benchmark data)"
echo ""
if ! $FULL_CLEAN && [[ -f "${VM_DIR}/ubuntu2404-base.img" ]]; then
    echo "  Tip: Run with --full to also delete the ~600 MB base image."
fi
echo ""
