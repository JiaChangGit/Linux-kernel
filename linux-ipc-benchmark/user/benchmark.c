/*
 * benchmark.c  —  Three-way IPC throughput comparison
 *
 * Test 1 — MQ  (kfifo + blocking syscall)
 *   write() / read() on /dev/mq_ipc
 *   Each message: copy_from_user + copy_to_user  (2 copies)
 *
 * Test 2 — SHM syscall  (spinlock ring-buffer + syscall)
 *   write() / read() on /dev/shm_ipc
 *   Same 2 copies as MQ; isolates queue-mechanism cost
 *
 * Test 3 — SHM mmap  (ring-buffer + direct memory access, zero-copy)
 *   mmap() on /dev/shm_ipc, then write/read via pointer arithmetic
 *   Zero copies; no per-message syscall; only a memory barrier per slot
 *
 * Each test spawns two POSIX threads (producer + consumer) that are
 * barrier-synchronised at start.  Wall-clock time is measured from first
 * thread start to last thread join.
 *
 * Build:  see user/Makefile  (links -lpthread)
 * Usage:  ./benchmark [message_count]   (default 200 000)
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "common.h"

/* ── default workload ──────────────────────────────────────────────── */
#define DEFAULT_COUNT 200000

/* ── thread argument / result ────────────────────────────────────────── */
typedef struct {
  int fd;            /* device fd (shared between threads)  */
  shm_region_t* shm; /* non-NULL only for mmap test         */
  int count;         /* messages to produce / consume       */
  int is_producer;
  pthread_barrier_t* bar; /* synchronised start                  */
  double elapsed_us;      /* filled by thread before return      */
} targ_t;

/* ── producer / consumer for MQ and SHM-syscall ─────────────────────── */
static void* syscall_worker(void* arg) {
  targ_t* a = (targ_t*)arg;
  char buf[MSG_SIZE];
  int i;
  ssize_t r;

  memset(buf, 0xAB, MSG_SIZE);
  pthread_barrier_wait(a->bar); /* sync both threads at start      */
  double t0 = now_us();

  for (i = 0; i < a->count; i++) {
    if (a->is_producer) {
      do {
        r = write(a->fd, buf, MSG_SIZE);
      } while (r < 0 && (errno == EAGAIN || errno == ENOSPC || errno == EINTR));
    } else {
      do {
        r = read(a->fd, buf, MSG_SIZE);
      } while (r < 0 && (errno == EAGAIN || errno == EINTR));
    }
  }

  a->elapsed_us = now_us() - t0;
  return NULL;
}

/* ── producer / consumer for SHM-mmap (zero-copy) ───────────────────── */
static void* mmap_worker(void* arg) {
  targ_t* a = (targ_t*)arg;
  shm_region_t* shm = a->shm;
  int i;

  pthread_barrier_wait(a->bar);
  double t0 = now_us();

  if (a->is_producer) {
    char fill[MSG_SIZE];
    memset(fill, 0xCD, MSG_SIZE);
    for (i = 0; i < a->count; i++) {
      uint32_t head, next;
      /* spin-wait while ring is full */
      do {
        head = shm->head.value;
        next = (head + 1) % RING_CAPACITY;
      } while (next == shm->tail.value);
      memcpy((void*)shm->data[head], fill, MSG_SIZE);
      __sync_synchronize();
      shm->head.value = next;
    }
  } else {
    char sink[MSG_SIZE];
    for (i = 0; i < a->count; i++) {
      uint32_t tail;
      /* spin-wait while ring is empty */
      do {
        tail = shm->tail.value;
      } while (tail == shm->head.value);
      __sync_synchronize();
      memcpy(sink, (void*)shm->data[tail], MSG_SIZE);
      shm->tail.value = (tail + 1) % RING_CAPACITY;
    }
  }

  a->elapsed_us = now_us() - t0;
  return NULL;
}

/* ── run one benchmark, return wall-clock time (µs) ─────────────────── */
static double run_test(int fd, int count, int use_mmap, shm_region_t* shm,
                       double* prod_us_out, double* cons_us_out) {
  pthread_t pt, ct;
  pthread_barrier_t bar;
  targ_t pa = {0}, ca = {0};
  double wall_start, wall_end;

  pthread_barrier_init(&bar, NULL, 2);
  pa.fd = ca.fd = fd;
  pa.shm = ca.shm = shm;
  pa.count = ca.count = count;
  pa.bar = ca.bar = &bar;
  pa.is_producer = 1;
  ca.is_producer = 0;

  wall_start = now_us();
  if (use_mmap) {
    pthread_create(&pt, NULL, mmap_worker, &pa);
    pthread_create(&ct, NULL, mmap_worker, &ca);
  } else {
    pthread_create(&pt, NULL, syscall_worker, &pa);
    pthread_create(&ct, NULL, syscall_worker, &ca);
  }
  pthread_join(pt, NULL);
  pthread_join(ct, NULL);
  wall_end = now_us();

  pthread_barrier_destroy(&bar);
  *prod_us_out = pa.elapsed_us;
  *cons_us_out = ca.elapsed_us;
  return wall_end - wall_start;
}

/* ── pretty helpers ──────────────────────────────────────────────────── */
static void bar_chart(double ratio, int width) {
  int filled = (int)(ratio * width);
  if (filled > width) filled = width;
  int i;
  printf("  [");
  for (i = 0; i < width; i++) putchar(i < filled ? '#' : '-');
  printf("]  x%.2f\n", ratio);
}

static void sep(void) {
  printf("──────────────────────────────────────────────────────────────\n");
}

/* ── main ────────────────────────────────────────────────────────────── */
int main(int argc, char* argv[]) {
  int count = (argc > 1) ? atoi(argv[1]) : DEFAULT_COUNT;
  int fd_mq = -1, fd_shm = -1;
  shm_region_t* shm = NULL;

  double wall[3], prod[3], cons[3], tp[3];

  /* ── open devices ── */
  fd_mq = open(MQ_DEVICE, O_RDWR);
  if (fd_mq < 0) {
    perror("open " MQ_DEVICE);
    return 1;
  }
  fd_shm = open(SHM_DEVICE, O_RDWR);
  if (fd_shm < 0) {
    perror("open " SHM_DEVICE);
    close(fd_mq);
    return 1;
  }

  shm = mmap(NULL, SHM_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
  if (shm == MAP_FAILED) {
    perror("mmap");
    goto done;
  }

  printf("\n");
  printf("══════════════════════════════════════════════════════════════\n");
  printf("  Linux IPC Benchmark  —  MQ vs Shared Memory               \n");
  printf("══════════════════════════════════════════════════════════════\n");
  printf("  msg count : %d\n", count);
  printf("  msg size  : %d bytes\n", MSG_SIZE);
  printf("  data vol  : %.1f MB\n", (double)count * MSG_SIZE / 1024.0 / 1024.0);
  printf("  ring cap  : %d slots\n", RING_CAPACITY);
  printf("\n");

  /* ━━━━ Test 1 : MQ (kfifo + syscall) ━━━━ */
  sep();
  printf("[1] Message Queue  (kfifo + blocking syscall)\n");
  printf("    copy path: write→copy_from_user→kfifo→copy_to_user→read\n\n");
  wall[0] = run_test(fd_mq, count, 0, NULL, &prod[0], &cons[0]);
  tp[0] = (double)count / (wall[0] / 1e6);
  printf("    producer : %8.1f ms  →  %12.0f msg/s\n", prod[0] / 1e3,
         (double)count / (prod[0] / 1e6));
  printf("    consumer : %8.1f ms  →  %12.0f msg/s\n", cons[0] / 1e3,
         (double)count / (cons[0] / 1e6));
  printf("    wall     : %8.1f ms  →  %12.0f msg/s  (combined)\n\n",
         wall[0] / 1e3, tp[0]);

  /* ━━━━ Test 2 : SHM syscall ━━━━ */
  sep();
  printf("[2] Shared Memory  (ring-buf + syscall,  spinlock)\n");
  printf("    copy path: write→copy_from_user→ring→copy_to_user→read\n\n");
  wall[1] = run_test(fd_shm, count, 0, NULL, &prod[1], &cons[1]);
  tp[1] = (double)count / (wall[1] / 1e6);
  printf("    producer : %8.1f ms  →  %12.0f msg/s\n", prod[1] / 1e3,
         (double)count / (prod[1] / 1e6));
  printf("    consumer : %8.1f ms  →  %12.0f msg/s\n", cons[1] / 1e3,
         (double)count / (cons[1] / 1e6));
  printf("    wall     : %8.1f ms  →  %12.0f msg/s  (combined)\n\n",
         wall[1] / 1e3, tp[1]);

  /* ━━━━ Test 3 : SHM mmap zero-copy ━━━━ */
  shm->head.value = shm->tail.value = 0; /* reset ring before this test     */
  sep();
  printf("[3] Shared Memory  (ring-buf + mmap,  ZERO-COPY)\n");
  printf("    copy path: producer memcpy→page  consumer memcpy←page\n");
  printf(
      "                *** no copy_from/to_user, no per-msg syscall ***\n\n");
  wall[2] = run_test(fd_shm, count, 1, shm, &prod[2], &cons[2]);
  tp[2] = (double)count / (wall[2] / 1e6);
  printf("    producer : %8.1f ms  →  %12.0f msg/s\n", prod[2] / 1e3,
         (double)count / (prod[2] / 1e6));
  printf("    consumer : %8.1f ms  →  %12.0f msg/s\n", cons[2] / 1e3,
         (double)count / (cons[2] / 1e6));
  printf("    wall     : %8.1f ms  →  %12.0f msg/s  (combined)\n\n",
         wall[2] / 1e3, tp[2]);

  /* ━━━━ Summary table ━━━━ */
  sep();
  printf("  SUMMARY  (baseline = MQ throughput)\n\n");
  const char* labels[] = {"[1] MQ  kfifo+syscall ", "[2] SHM ring+syscall  ",
                          "[3] SHM ring+mmap     "};
  int w = 28;
  for (int i = 0; i < 3; i++) {
    double ratio = tp[i] / tp[0];
    printf("  %s  %10.0f msg/s", labels[i], tp[i]);
    bar_chart(ratio, w);
  }

  printf("\n  Kernel-side latency stats\n");
  printf("  /proc/mq_stats:\n");
  system(
      "cat /proc/mq_stats  | grep -E 'avg_latency|enqueue|dequeue' "
      "| sed 's/^/    /'");
  printf("  /proc/shm_stats:\n");
  system(
      "cat /proc/shm_stats | grep -E 'avg_latency|write|read_count'  "
      "| sed 's/^/    /'");
  printf("\n");

done:
  if (shm && shm != MAP_FAILED) munmap(shm, SHM_MAP_SIZE);
  if (fd_mq >= 0) close(fd_mq);
  if (fd_shm >= 0) close(fd_shm);
  return 0;
}
