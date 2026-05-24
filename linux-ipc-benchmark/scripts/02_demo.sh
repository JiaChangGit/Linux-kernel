#!/usr/bin/env bash
# scripts/02_demo.sh  —  step-by-step IPC demonstration
set -euo pipefail

GREEN='\033[0;32m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'
hdr() { echo -e "\n${BOLD}${CYAN}$*${NC}"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# ── sanity ─────────────────────────────────────────────────────────
[[ $EUID -ne 0 ]] && { echo "Run as root"; exit 1; }
for dev in /dev/mq_ipc /dev/shm_ipc; do
    [[ -c $dev ]] || { echo "Missing $dev — run 01_setup.sh first"; exit 1; }
done

echo ""
echo "════════════════════════════════════════════════════════════════"
echo "  linux-ipc-benchmark  —  IPC Concept Demo"
echo "════════════════════════════════════════════════════════════════"
echo ""
echo "  This demo runs both mechanisms with a tiny message count so"
echo "  you can inspect every enqueue / dequeue step and compare the"
echo "  timing deltas side-by-side."
echo ""

# ── Part 1: Message Queue ──────────────────────────────────────────
hdr "━━━━  Part 1 of 2  —  Message Queue  (/dev/mq_ipc)  ━━━━"
cat <<'EXPLAIN'

  Mechanism : Linux kfifo (kernel FIFO ring-buffer)
  Sync      : mutex + wait-queue  (blocking sleep/wake)

  PRODUCER flow:
    write(fd, msg, 64)
      └─► copy_from_user(kbuf, msg, 64)   ← 1st copy  user→kernel
          └─► kfifo_in(&g_fifo, kbuf, 64)

  CONSUMER flow:
    read(fd, buf, 64)
      └─► kfifo_out(&g_fifo, kbuf, 64)
          └─► copy_to_user(buf, kbuf, 64) ← 2nd copy  kernel→user

  Every message pays the user↔kernel crossing TWICE.

EXPLAIN

read -rp "  Press Enter to run mq_demo…"
echo ""
"${PROJECT_DIR}/user/mq_demo"

# ── Part 2: Shared Memory mmap ─────────────────────────────────────
hdr "━━━━  Part 2 of 2  —  Shared Memory mmap  (/dev/shm_ipc)  ━━━━"
cat <<'EXPLAIN'

  Mechanism : vmalloc() ring-buffer exposed via mmap()
  Sync      : single-producer/single-consumer lock-free ring
               (only a memory barrier per slot, no mutex)

  SETUP (once):
    mmap(NULL, SHM_MAP_SIZE, PROT_RW, MAP_SHARED, fd, 0)
      └─► kernel maps g_shm physical pages into user VMA
          Both sides now see the SAME physical memory.

  PRODUCER flow (no syscall, no copy):
    shm->data[head] = msg;    ← write directly into shared page
    __sync_synchronize();     ← memory barrier
    shm->head.value = next;

  CONSUMER flow (no syscall, no copy):
    msg = shm->data[tail];    ← read directly from shared page
    shm->tail.value = (tail+1) % CAP;

  Zero extra copies.  Zero per-message syscalls.

EXPLAIN

read -rp "  Press Enter to run shm_demo…"
echo ""
"${PROJECT_DIR}/user/shm_demo"

# ── Side-by-side proc stats ────────────────────────────────────────
hdr "━━━━  Kernel-side stats  ━━━━"
echo ""
echo "  /proc/mq_stats"
cat /proc/mq_stats  | sed 's/^/    /'
echo ""
echo "  /proc/shm_stats"
cat /proc/shm_stats | sed 's/^/    /'

echo ""
echo -e "${GREEN}Demo complete.${NC}"
echo "  Next step:  sudo bash scripts/03_benchmark.sh"
echo ""
