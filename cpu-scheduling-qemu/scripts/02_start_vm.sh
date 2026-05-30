#!/usr/bin/env bash
# =============================================================================
# 02_start_vm.sh
#
# 啟動 QEMU VM，並等待 VM 內的 scheduler 準備完成。
#
# 重點：
#   - 有 /dev/kvm 權限時使用 KVM，加快 VM 開機
#   - 沒有 KVM 時改用 TCG，並拉長 timeout
#   - 不只等待 SSH，也等待 cloud-init 寫出 .setup_done
#   - QEMU 以 daemon 模式背景執行，PID 寫入 vm/qemu.pid
# =============================================================================

set -euo pipefail

VM_DIR="./vm"
DISK_NAME="${VM_DIR}/ubuntu2404.qcow2"
SEED_ISO="${VM_DIR}/seed.iso"
PID_FILE="${VM_DIR}/qemu.pid"
LOG_FILE="${VM_DIR}/qemu.log"
SERIAL_LOG="${VM_DIR}/qemu-serial.log"
SSH_PORT="2222"
VM_USER="scheduler"
VM_PASS="scheduler123"
VM_MEM="1024"
VM_CPUS="2"
BOOT_TIMEOUT=120
SETUP_TIMEOUT=180

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; NC='\033[0m'

info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
success() { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
die()     { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

SSH_OPTS=(
    -o StrictHostKeyChecking=no
    -o UserKnownHostsFile=/dev/null
    -o LogLevel=ERROR
    -o PreferredAuthentications=password
    -o PubkeyAuthentication=no
    -o ConnectTimeout=3
    -p "${SSH_PORT}"
)

vm_ssh() {
    sshpass -p "${VM_PASS}" ssh "${SSH_OPTS[@]}" "${VM_USER}@localhost" "$@"
}

[[ -f "$DISK_NAME" ]] || die "Disk image not found. Run scripts/01_setup_env.sh first."
[[ -f "$SEED_ISO"  ]] || die "Seed ISO not found. Run scripts/01_setup_env.sh first."
command -v sshpass &>/dev/null || die "sshpass is required. Re-run scripts/01_setup_env.sh."

if [[ -f "$PID_FILE" ]]; then
    OLD_PID=$(cat "$PID_FILE")
    if kill -0 "$OLD_PID" 2>/dev/null; then
        warn "VM is already running (PID ${OLD_PID}). Use scripts/05_cleanup.sh to stop it."
        exit 0
    fi
    rm -f "$PID_FILE"
fi

info "Starting QEMU VM (x86_64)..."
info "  Memory : ${VM_MEM} MB"
info "  CPUs   : ${VM_CPUS}"
info "  SSH    : localhost:${SSH_PORT}"

QEMU_ACCEL="tcg"
QEMU_MACHINE="pc"
QEMU_CPU="max"

if [[ -c /dev/kvm && -r /dev/kvm && -w /dev/kvm ]]; then
    QEMU_ACCEL="kvm"
    QEMU_MACHINE="q35"
    QEMU_CPU="host,+x2apic"
else
    # TCG 是純軟體模擬，速度較慢；legacy pc machine 在無 KVM 時相容性較穩。
    QEMU_CPU="qemu64"
    BOOT_TIMEOUT=480
    SETUP_TIMEOUT=240
fi

info "  Accel  : ${QEMU_ACCEL}"
info "  Machine: ${QEMU_MACHINE}"

: > "${LOG_FILE}"
: > "${SERIAL_LOG}"

if ! qemu-system-x86_64 \
    -name "cpu-scheduler-demo" \
    -machine "${QEMU_MACHINE}",accel="${QEMU_ACCEL}" \
    -cpu "${QEMU_CPU}" \
    -smp "${VM_CPUS}" \
    -m "${VM_MEM}" \
    -drive file="${DISK_NAME}",format=qcow2,if=virtio,cache=writeback \
    -drive file="${SEED_ISO}",format=raw,if=virtio,readonly=on \
    -netdev user,id=net0,hostfwd=tcp::"${SSH_PORT}"-:22 \
    -device virtio-net-pci,netdev=net0 \
    -display none \
    -monitor none \
    -serial "file:${SERIAL_LOG}" \
    -daemonize \
    -pidfile "${PID_FILE}" \
    >> "${LOG_FILE}" 2>&1; then
    die "QEMU failed to start. Check ${LOG_FILE} for details."
fi

[[ -f "$PID_FILE" ]] || die "QEMU failed to start. Check ${LOG_FILE} for details."

QEMU_PID=$(cat "$PID_FILE")
success "QEMU launched (PID ${QEMU_PID})"

info "Waiting for VM SSH to become available (up to ${BOOT_TIMEOUT}s)..."

elapsed=0
while ! vm_ssh true >/dev/null 2>&1; do
    if grep -q "Kernel panic -" "${SERIAL_LOG}" 2>/dev/null; then
        die "Guest kernel panicked during boot. Check ${SERIAL_LOG}."
    fi

    sleep 3
    elapsed=$((elapsed + 3))
    echo -ne "\r  Waiting... ${elapsed}s"

    if [[ $elapsed -ge $BOOT_TIMEOUT ]]; then
        echo ""
        die "Timed out waiting for SSH. Check ${LOG_FILE} and ${SERIAL_LOG}."
    fi
done
echo ""
success "SSH is ready."

info "Waiting for cloud-init to finish provisioning (up to ${SETUP_TIMEOUT}s)..."

elapsed=0
while true; do
    if vm_ssh "test -x /home/${VM_USER}/scheduler && grep -q '^SCHEDULER_READY$' /home/${VM_USER}/.setup_done" >/dev/null 2>&1; then
        success "Scheduler binary is ready inside the VM."
        break
    fi

    if vm_ssh "test -f /home/${VM_USER}/.setup_failed" >/dev/null 2>&1; then
        echo ""
        vm_ssh "cat /home/${VM_USER}/.setup_failed" || true
        die "Cloud-init reported setup failure. Check ${SERIAL_LOG}."
    fi

    if grep -q "Kernel panic -" "${SERIAL_LOG}" 2>/dev/null; then
        echo ""
        die "Guest kernel panicked during cloud-init. Check ${SERIAL_LOG}."
    fi

    sleep 3
    elapsed=$((elapsed + 3))
    echo -ne "\r  Waiting for cloud-init... ${elapsed}s"

    if [[ $elapsed -ge $SETUP_TIMEOUT ]]; then
        echo ""
        die "Cloud-init did not finish within ${SETUP_TIMEOUT}s. Check ${SERIAL_LOG} and ${LOG_FILE}."
    fi
done
echo ""

echo ""
echo "============================================================"
echo "  VM is up and scheduler is ready"
echo "============================================================"
echo ""
echo "  Connect  : ssh -p ${SSH_PORT} ${VM_USER}@localhost"
echo "  Password : ${VM_PASS}"
echo "  PID file : ${PID_FILE}"
echo "  QEMU log : ${LOG_FILE}"
echo "  Serial   : ${SERIAL_LOG}"
echo ""
echo "  Next step: bash scripts/03_demo.sh"
echo ""
