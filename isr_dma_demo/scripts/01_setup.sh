#!/usr/bin/env bash
# =============================================================================
#  01_setup.sh — Environment Setup
#  ISR + DMA Ring Buffer Demo | Ubuntu 24.04
# =============================================================================
#
#  This script installs all build dependencies, verifies kernel headers,
#  compiles the kernel module (.ko) and the userspace consumer binary.
#
#  Usage:  bash scripts/01_setup.sh
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_STAGING_DIR=""
KERNEL_BUILD_DIR=""

# ── Colour helpers ────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
error()   { echo -e "${RED}[ERROR]${RESET} $*" >&2; exit 1; }

cleanup() {
    if [[ -n "$BUILD_STAGING_DIR" && -d "$BUILD_STAGING_DIR" ]]; then
        rm -rf "$BUILD_STAGING_DIR"
    fi

    return 0
}

ascii_safe_path() {
    printf '%s' "$1" | LC_ALL=C grep -q '^[ -~]*$'
}

prepare_kernel_build_dir() {
    if ascii_safe_path "$ROOT_DIR"; then
        KERNEL_BUILD_DIR="$ROOT_DIR/kernel"
        return
    fi

    BUILD_STAGING_DIR="$(mktemp -d /tmp/isr_dma_demo_build.XXXXXX)"
    ln -s "$ROOT_DIR" "$BUILD_STAGING_DIR/src"
    warn "Project path contains non-ASCII characters." >&2
    warn "Building kernel module via ASCII-only symlink: $BUILD_STAGING_DIR/src" >&2
    KERNEL_BUILD_DIR="$BUILD_STAGING_DIR/src/kernel"
}

trap cleanup EXIT

echo -e "${BOLD}"
cat <<'BANNER'
 ___  ____  ____        ____  __  __    _
|_ _|/ ___||  _ \      |  _ \|  \/  |  / \
 | | \___ \| |_) |_____| | | | |\/| | / _ \
 | |  ___) |  _ <|_____| |_| | |  | |/ ___ \
|___||____/|_| \_\    |____/|_|  |_/_/   \_\

  ISR + DMA Ring Buffer Demo — Environment Setup
BANNER
echo -e "${RESET}"

# ── 1. Check Ubuntu version ───────────────────────────────────────────────────
info "Checking OS..."
if ! grep -q "Ubuntu" /etc/os-release 2>/dev/null; then
    warn "This demo is designed for Ubuntu 24.04. Proceeding anyway."
fi
OS_VER=$(. /etc/os-release && echo "$VERSION_ID")
info "Detected OS: Ubuntu $OS_VER"

# ── 2. Install build dependencies ────────────────────────────────────────────
info "Installing build dependencies (requires sudo)..."
sudo apt-get update -qq
sudo apt-get install -y -qq \
    build-essential \
    linux-headers-"$(uname -r)" \
    kmod \
    bc \
    flex \
    bison \
    libssl-dev \
    libelf-dev \
    gcc \
    make \
    git \
    python3 \
    python3-pip \
    gnuplot \
    2>/dev/null || true

success "Build dependencies installed."

# ── 3. Verify kernel headers exist ───────────────────────────────────────────
KDIR="/lib/modules/$(uname -r)/build"
info "Verifying kernel headers at: $KDIR"
if [[ ! -d "$KDIR" ]]; then
    error "Kernel headers not found at $KDIR.
    Try: sudo apt-get install linux-headers-$(uname -r)"
fi
success "Kernel headers found."

# ── 4. Build kernel module ────────────────────────────────────────────────────
info "Building kernel module..."
prepare_kernel_build_dir
cd "$KERNEL_BUILD_DIR"
make clean 2>/dev/null || true
if ! make 2>&1; then
    error "Kernel module build failed. Full compiler output shown above."
fi
if [[ ! -f "isr_dma_module.ko" ]]; then
    error "Kernel module build failed. Check output above."
fi
success "Kernel module built: kernel/isr_dma_module.ko"

# Print module info
echo ""
echo -e "${CYAN}Module info:${RESET}"
modinfo isr_dma_module.ko | grep -E "^(filename|description|author|license|version)"
echo ""

# ── 5. Build userspace consumer ───────────────────────────────────────────────
info "Building userspace consumer..."
cd "$ROOT_DIR/userspace"
make clean 2>/dev/null || true
make 2>&1
if [[ ! -f "consumer" ]]; then
    error "Userspace build failed."
fi
success "Userspace consumer built: userspace/consumer"

# ── 6. Summary ────────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}${BOLD}════════════════════════════════════════════════${RESET}"
echo -e "${GREEN}${BOLD}  Setup complete! Files ready:${RESET}"
echo -e "  ${CYAN}kernel/isr_dma_module.ko${RESET}  ← kernel module"
echo -e "  ${CYAN}userspace/consumer${RESET}         ← userspace app"
echo ""
echo -e "  Next step: ${BOLD}bash scripts/02_demo.sh${RESET}"
echo -e "${GREEN}${BOLD}════════════════════════════════════════════════${RESET}"
