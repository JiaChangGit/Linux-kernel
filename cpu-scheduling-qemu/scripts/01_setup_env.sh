#!/usr/bin/env bash
# =============================================================================
# 01_setup_env.sh
#
# 建立 QEMU + Ubuntu 24.04 實驗環境。
#
# 流程：
#   1. 檢查 Host 端工具是否存在
#   2. 下載 Ubuntu 24.04 LTS cloud image
#   3. 複製並擴充 qcow2 disk
#   4. 在 Host 端先編譯 scheduler.c，提早抓出 C 語法錯誤
#   5. 將 scheduler binary、source code、workload 透過 base64 放進 cloud-init
#   6. 產生 seed.iso，讓 VM 開機後自動佈署檔案
#
# Host 端套件：
#   sudo apt install -y qemu-system-x86 qemu-utils cloud-image-utils \
#       libguestfs-tools gcc wget sshpass openssl
# =============================================================================

set -euo pipefail

# 可調整參數集中放在前面，方便更換 Ubuntu image、VM 規格或 SSH port。
UBUNTU_URL="https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img"
IMAGE_NAME="ubuntu2404-base.img"
DISK_NAME="ubuntu2404.qcow2"
SEED_ISO="seed.iso"
VM_DIR="./vm"
VM_USER="scheduler"
VM_PASS="scheduler123"          # 本機 demo 密碼，不適合正式環境。
VM_MEM="1024"                   # MB
VM_CPUS="2"
DISK_SIZE="8G"
SSH_PORT="2222"

# 統一訊息格式；錯誤一律走 die()，避免腳本在半完成狀態繼續跑。
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; NC='\033[0m'

info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
success() { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
die()     { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

info "Checking host dependencies..."

REQUIRED=(qemu-system-x86_64 qemu-img cloud-localds virt-customize gcc wget sshpass openssl)
MISSING=()
for cmd in "${REQUIRED[@]}"; do
    command -v "$cmd" &>/dev/null || MISSING+=("$cmd")
done

if [[ ${#MISSING[@]} -gt 0 ]]; then
    warn "Missing tools: ${MISSING[*]}"
    info "Attempting to install missing packages..."
    sudo apt-get update -qq
    sudo apt-get install -y \
        qemu-system-x86 \
        qemu-utils \
        cloud-image-utils \
        libguestfs-tools \
        gcc \
        sshpass \
        wget \
        openssl \
        2>/dev/null || die "Package installation failed. Please install the required tools manually."
fi
success "All dependencies are available."

mkdir -p "$VM_DIR"
cd "$VM_DIR"

if [[ -f "$IMAGE_NAME" ]]; then
    info "Base image already exists; skipping download."
else
    info "Downloading Ubuntu 24.04 cloud image, about 600 MB..."
    wget -q --show-progress -O "$IMAGE_NAME" "$UBUNTU_URL"
    success "Download complete: $IMAGE_NAME"
fi

if [[ ! -f "$DISK_NAME" ]]; then
    info "Creating working qcow2 disk..."
    qemu-img convert -f qcow2 -O qcow2 "$IMAGE_NAME" "$DISK_NAME"
    info "Resizing disk to ${DISK_SIZE}..."
    qemu-img resize "$DISK_NAME" "$DISK_SIZE"
    success "Disk image ready: $DISK_NAME"
else
    info "Disk image already exists; skipping conversion."
fi

info "Compiling scheduler.c on host for pre-flight verification..."
cd ..
gcc -O2 -Wall -Wextra -std=c11 -o /tmp/scheduler_host_check src/scheduler.c \
    && success "Host compilation passed." \
    || die "scheduler.c failed to compile. Fix the source first."

[[ -f src/workload_demo.txt  ]] || die "Missing workload file: src/workload_demo.txt"
[[ -f src/workload_bench.txt ]] || die "Missing workload file: src/workload_bench.txt"

SCHEDULER_B64=$(base64 -w 0 src/scheduler.c)
SCHEDULER_BIN_B64=$(base64 -w 0 /tmp/scheduler_host_check)
WORKLOAD_DEMO_B64=$(base64 -w 0 src/workload_demo.txt)
WORKLOAD_BENCH_B64=$(base64 -w 0 src/workload_bench.txt)

cd "$VM_DIR"

info "Generating cloud-init user-data and meta-data..."

cat > user-data <<EOF
#cloud-config
hostname: scheduler-vm
manage_etc_hosts: true
ssh_pwauth: true

users:
  - name: ${VM_USER}
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    lock_passwd: false
    passwd: $(echo "${VM_PASS}" | openssl passwd -6 -stdin)

package_update: false
package_upgrade: false

runcmd:
  - |
      install -d -o ${VM_USER} -g ${VM_USER} /home/${VM_USER}
      rm -f /home/${VM_USER}/.setup_done /home/${VM_USER}/.setup_failed
      if ! (
          set -e
          cat <<'EOF' | base64 -d > /home/${VM_USER}/scheduler
      ${SCHEDULER_BIN_B64}
      EOF
          cat <<'EOF' | base64 -d > /home/${VM_USER}/scheduler.c
      ${SCHEDULER_B64}
      EOF
          cat <<'EOF' | base64 -d > /home/${VM_USER}/workload_demo.txt
      ${WORKLOAD_DEMO_B64}
      EOF
          cat <<'EOF' | base64 -d > /home/${VM_USER}/workload_bench.txt
      ${WORKLOAD_BENCH_B64}
      EOF
          chown ${VM_USER}:${VM_USER} /home/${VM_USER}/scheduler /home/${VM_USER}/scheduler.c /home/${VM_USER}/workload_demo.txt /home/${VM_USER}/workload_bench.txt
          chmod 0755 /home/${VM_USER}/scheduler
          chmod 0644 /home/${VM_USER}/scheduler.c /home/${VM_USER}/workload_demo.txt /home/${VM_USER}/workload_bench.txt
      ); then
          echo "SCHEDULER_SETUP_FAILED" > /home/${VM_USER}/.setup_failed
          echo "Failure at \$(date)" >> /home/${VM_USER}/.setup_failed
          exit 1
      fi
      echo "SCHEDULER_READY" > /home/${VM_USER}/.setup_done
      echo "Build complete at \$(date)" >> /home/${VM_USER}/.setup_done
EOF

cat > meta-data <<EOF
instance-id: scheduler-vm-001
local-hostname: scheduler-vm
EOF

info "Building cloud-init seed ISO..."
cloud-localds "$SEED_ISO" user-data meta-data
success "Seed ISO created: ${VM_DIR}/${SEED_ISO}"

echo ""
echo "============================================================"
echo "  Environment setup complete"
echo "============================================================"
echo ""
echo "  Disk image : ${VM_DIR}/${DISK_NAME}"
echo "  Seed ISO   : ${VM_DIR}/${SEED_ISO}"
echo "  VM user    : ${VM_USER} / ${VM_PASS}"
echo "  SSH port   : ${SSH_PORT} (host -> VM)"
echo ""
echo "  Next step  : bash scripts/02_start_vm.sh"
echo ""
