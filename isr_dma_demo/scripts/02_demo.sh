#!/usr/bin/env bash
# =============================================================================
#  02_demo.sh — Live Demonstration of ISR + DMA Ring Buffer
#  ISR + DMA Ring Buffer Demo | Ubuntu 24.04
# =============================================================================
#
#  This script:
#    1. Loads the kernel module (insmod)
#    2. Shows /proc/isr_dma_stats in real-time
#    3. Runs the userspace consumer (read path demo)
#    4. Visualises the ring buffer state
#
#  Usage:  sudo bash scripts/02_demo.sh
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BLUE='\033[0;34m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
error()   { echo -e "${RED}[ERROR]${RESET} $*" >&2; exit 1; }
step()    { echo -e "\n${BOLD}${BLUE}──── Step $1: $2 ────${RESET}"; }

# ── Root check ────────────────────────────────────────────────────────────────
if [[ $EUID -ne 0 ]]; then
    error "This script must be run as root (for insmod/rmmod).\n  Try: sudo bash scripts/02_demo.sh"
fi

# ── Pre-flight checks ─────────────────────────────────────────────────────────
[[ -f "$ROOT_DIR/kernel/isr_dma_module.ko" ]] || \
    error "Kernel module not built. Run: bash scripts/01_setup.sh"

[[ -f "$ROOT_DIR/userspace/consumer" ]] || \
    error "Consumer not built. Run: bash scripts/01_setup.sh"

echo -e "${BOLD}"
cat <<'BANNER'
  ╔══════════════════════════════════════════════════════╗
  ║   ISR + DMA Ring Buffer — Live Demo                 ║
  ╚══════════════════════════════════════════════════════╝
BANNER
echo -e "${RESET}"

# ── Step 1: Load kernel module ────────────────────────────────────────────────
step 1 "Load Kernel Module"

# Unload if already loaded
if lsmod | grep -q isr_dma_module; then
    info "Module already loaded — reloading..."
    rmmod isr_dma_module
    sleep 0.5
fi

info "Loading isr_dma_module.ko ..."
insmod "$ROOT_DIR/kernel/isr_dma_module.ko"
sleep 0.5

if lsmod | grep -q isr_dma_module; then
    success "Module loaded successfully."
else
    error "Module load failed. Check: dmesg | tail -20"
fi

# Show dmesg output from module load
echo -e "\n${CYAN}Kernel log (module init):${RESET}"
dmesg | grep -i isr_dma | tail -8 | sed 's/^/  /'

# ── Step 2: Device node check ─────────────────────────────────────────────────
step 2 "Device Node Verification"
sleep 1  # udev needs time to create the node

if [[ -c /dev/isr_dma ]]; then
    success "/dev/isr_dma device node created by udev."
    ls -la /dev/isr_dma | sed 's/^/  /'
else
    warn "/dev/isr_dma not yet visible — trying manual mknod..."
    MAJOR=$(grep isr_dma /proc/devices | awk '{print $1}')
    if [[ -n "$MAJOR" ]]; then
        mknod /dev/isr_dma c "$MAJOR" 0
        chmod 666 /dev/isr_dma
        success "Device node created manually: major=$MAJOR"
    else
        error "Cannot find device major number. Check dmesg."
    fi
fi
chmod 666 /dev/isr_dma

# ── Step 3: /proc stats — observe ISR firing ──────────────────────────────────
step 3 "Observe ISR Firing via /proc/isr_dma_stats"

info "Opening device to start ISR timer, then watching /proc for 3 seconds..."
# Open and keep device open in background so ISR runs
exec 9<>/dev/isr_dma  # open fd 9

echo ""
for i in 1 2 3; do
    echo -e "${YELLOW}--- /proc/isr_dma_stats @ t=${i}s ---${RESET}"
    cat /proc/isr_dma_stats 2>/dev/null | sed 's/^/  /' || echo "  (not available)"
    sleep 1
done

exec 9<&-  # close fd 9 → ISR stops

echo ""
echo -e "${CYAN}Final kernel stats after 3s:${RESET}"
cat /proc/isr_dma_stats 2>/dev/null | sed 's/^/  /' || true

# ── Step 4: Ring buffer visualisation ────────────────────────────────────────
step 4 "Ring Buffer Architecture Visualisation"
cat <<'DIAGRAM'

  ┌─────────────────────────────────────────────────────────┐
  │               DMA-Coherent Ring Buffer                  │
  │  (physically contiguous, cache-coherent, shared memory) │
  │                                                         │
  │  ┌───────┬───────┬───────┬─────┬───────┬───────┐       │
  │  │ slot0 │ slot1 │ slot2 │ ... │N-2    │N-1    │       │
  │  │64 B   │64 B   │64 B   │     │64 B   │64 B   │       │
  │  └───────┴───────┴───────┴─────┴───────┴───────┘       │
  │       ▲                              ▲                  │
  │     TAIL (consumer)               HEAD (ISR producer)  │
  │                                                         │
  │  Control Block:  head=atomic32  tail=atomic32           │
  │                  isr_count      drop_count              │
  └─────────────────────────────────────────────────────────┘

  Producer (ISR/hrtimer):                                   
    1. Check ring_full(head, tail)                          
    2. Write 64-byte payload to data[head * SLOT_SIZE]      
    3. Atomic advance of head (release barrier)             

  Consumer (userspace):                                     
    Path A — mmap (zero-copy):                              
      1. Read data[tail * SLOT_SIZE] directly from mapping  
      2. Atomic advance of tail (acquire barrier)           
    Path B — read() syscall:                                
      1. Kernel reads slot, copy_to_user() to userspace buf 

DIAGRAM

# ── Step 5: Quick functional demo (read a few slots) ──────────────────────────
step 5 "Quick Functional Read Demo"

info "Reading 5 slots from /dev/isr_dma via hexdump..."
echo ""

# Open device (starts ISR), read 5 * 64 = 320 bytes
exec 9<>/dev/isr_dma
sleep 0.2  # let ISR fill some slots

dd if=/dev/isr_dma bs=64 count=5 2>/dev/null | hexdump -C | head -40 | sed 's/^/  /'

exec 9<&-

echo ""
info "Each 64-byte slot layout:"
echo "  Bytes  0- 7:  64-bit kernel timestamp (ns)"
echo "  Bytes  8-15:  ISR invocation counter"
echo "  Bytes 16-63:  0xAB pattern fill"

# ── Step 6: Show dmesg summary ────────────────────────────────────────────────
step 6 "Kernel Log Summary"
echo -e "${CYAN}Recent dmesg from isr_dma:${RESET}"
dmesg | grep isr_dma | tail -15 | sed 's/^/  /'

echo ""
echo -e "${GREEN}${BOLD}════════════════════════════════════════════════${RESET}"
echo -e "${GREEN}${BOLD}  Demo complete!${RESET}"
echo -e "  Module is still loaded. Next step:"
echo -e "  ${BOLD}sudo bash scripts/03_benchmark.sh${RESET}"
echo -e "${GREEN}${BOLD}════════════════════════════════════════════════${RESET}"
