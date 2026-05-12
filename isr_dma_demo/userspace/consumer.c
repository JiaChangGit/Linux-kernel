/*
 * consumer.c — Userspace consumer + benchmark for ISR/DMA Ring Buffer
 *
 * This program demonstrates two access paths to the kernel ring buffer:
 *
 *   Path A (ISR + DMA zero-copy):
 *     mmap() the DMA-coherent ring buffer directly into our address space.
 *     The ISR (hrtimer in kernel) writes to ring slots; we read by advancing
 *     the tail pointer — zero kernel/user copies needed.
 *
 *   Path B (Traditional read() syscall):
 *     Call read() for each slot — the kernel copies from ring to userspace
 *     via copy_to_user(). Standard but involves one extra memcpy per slot.
 *
 * Benchmark output is written both to stdout and to bench_results.txt
 * for later collection by the shell scripts.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ─── Must match kernel constants ─────────────────────────────── */
#define DEVICE_PATH "/dev/isr_dma"
#define RING_SLOTS 256
#define SLOT_SIZE 64
#define RING_TOTAL (RING_SLOTS * SLOT_SIZE)

/* ─── Ring control block layout (mirrors kernel struct) ─────── */
typedef struct {
  volatile int32_t head; /* atomic in kernel, volatile here */
  volatile int32_t tail;
  uint32_t slots;
  uint32_t slot_size;
  volatile uint64_t isr_count;
  volatile uint64_t drop_count;
} ring_ctrl_t;

/* ─── Helpers ────────────────────────────────────────────────── */
static inline uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static inline int ring_next(int idx) { return (idx + 1) & (RING_SLOTS - 1); }

static void print_bar(const char* label, uint64_t value, uint64_t max_val,
                      int width) {
  int filled = (int)((double)value / max_val * width);
  if (filled > width) filled = width;
  printf("  %-18s [", label);
  for (int i = 0; i < width; i++) putchar(i < filled ? '#' : '.');
  printf("] %8.2f ns/op\n", (double)value);
}

/* ─── Benchmark: Path A — mmap zero-copy ─────────────────────── */
static uint64_t bench_mmap(int fd, int duration_sec, uint64_t* ops_out) {
  /* Map enough pages to cover ctrl + data */
  size_t map_size =
      sysconf(_SC_PAGE_SIZE) *
      ((sizeof(ring_ctrl_t) + RING_TOTAL + sysconf(_SC_PAGE_SIZE) - 1) /
           sysconf(_SC_PAGE_SIZE) +
       1);

  uint8_t* base =
      mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (base == MAP_FAILED) {
    perror("mmap");
    return 0;
  }

  ring_ctrl_t* ctrl = (ring_ctrl_t*)base;

  /* Data area starts after the control block (aligned to SLOT_SIZE) */
  size_t data_off =
      ((sizeof(ring_ctrl_t) + SLOT_SIZE - 1) / SLOT_SIZE) * SLOT_SIZE;
  uint8_t* data = base + data_off;

  uint64_t ops = 0;
  uint64_t t_start = now_ns();
  uint64_t t_end = t_start + (uint64_t)duration_sec * 1000000000ULL;
  uint64_t latency_sum = 0;

  uint8_t shadow[SLOT_SIZE];

  while (now_ns() < t_end) {
    int head = __atomic_load_n(&ctrl->head, __ATOMIC_ACQUIRE);
    int tail = __atomic_load_n(&ctrl->tail, __ATOMIC_RELAXED);

    if (head == tail) {
      /* Ring empty — brief pause to avoid hammering the CPU */
      struct timespec ns = {0, 10000}; /* 10 µs */
      nanosleep(&ns, NULL);
      continue;
    }

    uint64_t t0 = now_ns();

    /* Zero-copy read: just reference the mapped memory */
    memcpy(shadow, data + tail * SLOT_SIZE, SLOT_SIZE);
    (void)shadow; /* prevent optimization */

    /* Advance tail to release the slot back to producer */
    __atomic_store_n(&ctrl->tail, ring_next(tail), __ATOMIC_RELEASE);

    latency_sum += now_ns() - t0;
    ops++;
  }

  munmap(base, map_size);
  *ops_out = ops;
  return ops ? latency_sum / ops : 0;
}

/* ─── Benchmark: Path B — read() syscall ─────────────────────── */
static uint64_t bench_read_syscall(int fd, int duration_sec,
                                   uint64_t* ops_out) {
  uint8_t buf[SLOT_SIZE];
  uint64_t ops = 0;
  uint64_t t_start = now_ns();
  uint64_t t_end = t_start + (uint64_t)duration_sec * 1000000000ULL;
  uint64_t latency_sum = 0;

  while (now_ns() < t_end) {
    uint64_t t0 = now_ns();
    ssize_t n = read(fd, buf, SLOT_SIZE);

    if (n == SLOT_SIZE) {
      latency_sum += now_ns() - t0;
      ops++;
    } else if (n == 0) {
      /* Ring empty */
      struct timespec ns = {0, 10000};
      nanosleep(&ns, NULL);
    } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
      perror("read");
      break;
    }
  }

  *ops_out = ops;
  return ops ? latency_sum / ops : 0;
}

/* ─── Print formatted report ─────────────────────────────────── */
static void print_report(const char* title, uint64_t mmap_lat,
                         uint64_t mmap_ops, uint64_t read_lat,
                         uint64_t read_ops, FILE* out) {
  double speedup = (read_lat && mmap_lat) ? (double)read_lat / mmap_lat : 0.0;

  const char* banner =
      "╔══════════════════════════════════════════════════════╗\n"
      "║          ISR + DMA Ring Buffer  Benchmark           ║\n"
      "╚══════════════════════════════════════════════════════╝\n";

  fprintf(out, "\n%s\n", banner);
  fprintf(out, "  Test: %s\n\n", title);
  fprintf(out, "  %-28s %12s  %12s\n", "Path", "Avg lat (ns)", "Total ops");
  fprintf(out, "  %s\n", "──────────────────────────────────────────────────");
  fprintf(out, "  %-28s %12.1f  %12llu\n", "A) ISR+DMA mmap (zero-copy)",
          (double)mmap_lat, (unsigned long long)mmap_ops);
  fprintf(out, "  %-28s %12.1f  %12llu\n", "B) read() syscall (copy)",
          (double)read_lat, (unsigned long long)read_ops);
  fprintf(out, "\n  Speedup  (B / A): %.2fx\n", speedup);
  fprintf(out, "\n  Throughput:\n");
  if (mmap_lat)
    fprintf(out, "    mmap  path: ~%llu Mops/s\n",
            (unsigned long long)(1000000000ULL / mmap_lat));
  if (read_lat)
    fprintf(out, "    read  path: ~%llu Mops/s\n",
            (unsigned long long)(1000000000ULL / read_lat));

  fprintf(out, "\n  ASCII Bar Chart (lower = faster):\n");
  uint64_t max_lat = mmap_lat > read_lat ? mmap_lat : read_lat;
  if (max_lat == 0) max_lat = 1;

  /* Redirect bar to stdout even when out is a file */
  if (out != stdout) {
    print_bar("A) mmap (zero-copy)", mmap_lat, max_lat, 40);
    print_bar("B) read() syscall", read_lat, max_lat, 40);
  }
  fprintf(out,
          "\n  Note: 'zero-copy' avoids copy_to_user() — ISR writes\n"
          "        directly to DMA-coherent memory visible from user.\n\n");
}

/* ─── main ───────────────────────────────────────────────────── */
int main(int argc, char* argv[]) {
  int duration = (argc > 1) ? atoi(argv[1]) : 5; /* seconds per run */
  if (duration < 1 || duration > 60) duration = 5;

  printf("\n[consumer] Opening %s ...\n", DEVICE_PATH);
  int fd = open(DEVICE_PATH, O_RDWR);
  if (fd < 0) {
    perror("open " DEVICE_PATH);
    fprintf(stderr,
            "Hint: is the kernel module loaded?\n"
            "      sudo insmod kernel/isr_dma_module.ko\n");
    return 1;
  }

  /* Reset stats and ring before each run */
  ioctl(fd, 0);
  printf("[consumer] Device opened. ISR timer running at 2000 IRQ/s.\n");
  printf("[consumer] Each slot: %d bytes, ring depth: %d slots.\n\n", SLOT_SIZE,
         RING_SLOTS);

  /* ── Run A: mmap zero-copy ── */
  printf("=== Run A: mmap zero-copy path (%d s) ===\n", duration);
  fflush(stdout);
  uint64_t mmap_ops, read_ops;
  uint64_t mmap_lat = bench_mmap(fd, duration, &mmap_ops);

  /* Reset between runs */
  ioctl(fd, 0);
  sleep(1);

  /* ── Run B: read() syscall ── */
  printf("=== Run B: read() syscall path (%d s) ===\n", duration);
  fflush(stdout);
  uint64_t read_lat = bench_read_syscall(fd, duration, &read_ops);

  /* ── Print report to stdout ── */
  print_report("mmap vs read() on /dev/isr_dma", mmap_lat, mmap_ops, read_lat,
               read_ops, stdout);
  print_bar("A) mmap (zero-copy)", mmap_lat,
            mmap_lat > read_lat ? mmap_lat : read_lat, 40);
  print_bar("B) read() syscall", read_lat,
            mmap_lat > read_lat ? mmap_lat : read_lat, 40);

  /* ── Save machine-readable results ── */
  FILE* rf = fopen("bench_results.txt", "w");
  if (rf) {
    fprintf(rf, "mmap_lat_ns=%llu\n", (unsigned long long)mmap_lat);
    fprintf(rf, "mmap_ops=%llu\n", (unsigned long long)mmap_ops);
    fprintf(rf, "read_lat_ns=%llu\n", (unsigned long long)read_lat);
    fprintf(rf, "read_ops=%llu\n", (unsigned long long)read_ops);
    double speedup = (read_lat && mmap_lat) ? (double)read_lat / mmap_lat : 0.0;
    fprintf(rf, "speedup=%.2f\n", speedup);
    fclose(rf);
    printf("[consumer] Results saved to bench_results.txt\n");
  }

  close(fd);
  return 0;
}
