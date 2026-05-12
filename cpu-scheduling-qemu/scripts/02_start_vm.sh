#!/usr/bin/env bash
# =============================================================================
# 02_start_vm.sh — Launch the QEMU VM and wait until it is SSH-ready
#
# Architecture : x86_64  (compatible with Ubuntu 24.04 on any x86 host)
# Machine type : q35 with KVM, pc with TCG fallback for broader compatibility
# Networking   : user-mode NAT, host port 2222 → guest port 22
#
# The VM boots in the background via QEMU's daemon mode. This script blocks
# until SSH is reachable and cloud-init finishes building the scheduler.
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
BOOT_TIMEOUT=120   # seconds to wait for SSH to become available
SETUP_TIMEOUT=180  # seconds to wait for cloud-init to finish in-VM build

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

# ── Pre-flight checks ─────────────────────────────────────────────────────────
[[ -f "$DISK_NAME" ]] || die "Disk image not found. Run scripts/01_setup_env.sh first."
[[ -f "$SEED_ISO"  ]] || die "Seed ISO not found. Run scripts/01_setup_env.sh first."
command -v sshpass &>/dev/null || die "sshpass is required. Re-run scripts/01_setup_env.sh."

if [[ -f "$PID_FILE" ]]; then
    OLD_PID=$(cat "$PID_FILE")
    if kill -0 "$OLD_PID" 2>/dev/null; then
        warn "VM is already running (PID ${OLD_PID}). Use 05_cleanup.sh to stop it."
        exit 0
    else
        rm -f "$PID_FILE"
    fi
fi

# ── Launch QEMU ───────────────────────────────────────────────────────────────
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
    # TCG is slower and has fewer chipset corner cases with the legacy pc machine.
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

if [[ ! -f "$PID_FILE" ]]; then
    die "QEMU failed to start. Check ${LOG_FILE} for details."
fi

QEMU_PID=$(cat "$PID_FILE")
success "QEMU launched (PID ${QEMU_PID})"

# ── Wait for SSH ──────────────────────────────────────────────────────────────
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
        die "Timed out waiting for SSH. Check ${LOG_FILE}."
    fi
done
echo ""
success "VM is ready!"

# ── Wait for cloud-init to finish provisioning the scheduler ─────────────────
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

# ── Print connection info ─────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  VM is up and ready${NC}"
echo -e "${GREEN}════════════════════════════════════════════════════${NC}"
echo ""
echo "  Connect  : ssh -p ${SSH_PORT} ${VM_USER}@localhost"
echo "  Password : scheduler123"
echo "  PID file : ${PID_FILE}"
echo "  QEMU log : ${LOG_FILE}"
echo "  Serial   : ${SERIAL_LOG}"
echo ""
echo "  Next step: bash scripts/03_demo.sh"
echo ""
