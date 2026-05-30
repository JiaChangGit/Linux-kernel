#!/usr/bin/env bash
# =============================================================================
# 05_cleanup.sh
#
# 關閉 QEMU VM，並移除本次建立的 VM 產物。
#
# 預設保留：
#   results/               demo 與 benchmark 結果
#   vm/ubuntu2404-base.img 下載好的 base image，方便下次快速建立
#
# 使用 --full 時，會連 base image 一起刪除。
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

if [[ -f "$PID_FILE" ]]; then
    QEMU_PID=$(cat "$PID_FILE")

    if kill -0 "$QEMU_PID" 2>/dev/null; then
        info "Stopping QEMU VM (PID ${QEMU_PID})..."
        kill -TERM "$QEMU_PID" 2>/dev/null || true
        sleep 3

        if kill -0 "$QEMU_PID" 2>/dev/null; then
            warn "VM did not stop after SIGTERM; sending SIGKILL."
            kill -KILL "$QEMU_PID" 2>/dev/null || true
        fi

        success "QEMU process stopped."
    else
        warn "PID file exists, but process ${QEMU_PID} is not running."
    fi

    rm -f "$PID_FILE"
else
    info "No PID file found; VM may already be stopped."
fi

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

if [[ -d "$VM_DIR" ]] && [[ -z "$(ls -A "$VM_DIR" 2>/dev/null)" ]]; then
    rmdir "$VM_DIR"
    info "Removed empty directory: ${VM_DIR}/"
fi

echo ""
success "Cleanup complete."
echo ""
echo "  Retained: ./results/   (demo output and benchmark data)"
echo ""
if ! $FULL_CLEAN && [[ -f "${VM_DIR}/ubuntu2404-base.img" ]]; then
    echo "  Tip: Run with --full to also delete the base image."
fi
echo ""
