/*
 * benchmark.c - 三種 IPC 路徑的吞吐量比較
 *
 * Test 1：MQ（kfifo + blocking syscall）
 *   對 /dev/mq_ipc 呼叫 write() / read()。
 *   每筆訊息會經過 copy_from_user() 與 copy_to_user()，共 2 次複製。
 *
 * Test 2：SHM syscall（spinlock ring buffer + syscall）
 *   對 /dev/shm_ipc 呼叫 write() / read()。
 *   同樣保留 2 次複製，用來和 MQ 對照 queue/ring 機制成本。
 *
 * Test 3：SHM mmap（mapped ring buffer + direct memory access）
 *   先 mmap() /dev/shm_ipc，再直接操作 shm_region_t。
 *   每筆訊息不再走 read/write syscall，也不再做 user/kernel copy。
 *
 * 每次測試都建立兩個 POSIX threads：producer 與 consumer。
 * pthread_barrier 讓兩邊同時起跑，wall-clock time 從建立測試開始量到
 * 兩個 thread 都結束為止。
 *
 * 建置：見 user/Makefile，benchmark 需連結 pthread。
 * 用法：./benchmark [message_count]，預設 200000 筆訊息。
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

/* ── 預設測試量 ───────────────────────────────────────────────────── */
#define DEFAULT_COUNT 200000

/* ── 傳給 worker thread 的參數與回傳統計 ───────────────────────────── */
typedef struct {
  int fd;            /* 兩個 thread 共用的 device fd。 */
  shm_region_t* shm; /* 只有 mmap 測試會設定，syscall 測試為 NULL。 */
  int count;         /* producer/consumer 各自處理的訊息數。 */
  int is_producer;
  pthread_barrier_t* bar; /* 讓 producer 與 consumer 同步起跑。 */
  double elapsed_us;      /* worker 結束前填入自己的耗時。 */
} targ_t;

/* ── MQ 與 SHM syscall 共用的 producer / consumer worker ───────────── */
static void* syscall_worker(void* arg) {
  targ_t* a = (targ_t*)arg;
  char buf[MSG_SIZE];
  int i;
  ssize_t r;

  memset(buf, 0xAB, MSG_SIZE);
  pthread_barrier_wait(a->bar); /* 等兩個 thread 都準備好再開始量測。 */
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

/* ── SHM mmap worker：直接操作 mapped ring，不經每筆 syscall ───────── */
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
      /* ring 滿時原地輪詢；benchmark 用它觀察低延遲路徑成本。 */
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
      /* ring 空時原地輪詢；consumer 等 producer 推進 head。 */
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

/* ── 執行單一測試，回傳 wall-clock time，單位為微秒 ────────────────── */
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

/* ── 輸出輔助函式：只負責顯示，不影響 benchmark 路徑 ─────────────── */
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

/* ── main：開啟裝置、建立 mmap、依序執行三種測試 ─────────────────── */
int main(int argc, char* argv[]) {
  int count = (argc > 1) ? atoi(argv[1]) : DEFAULT_COUNT;
  int fd_mq = -1, fd_shm = -1;
  shm_region_t* shm = NULL;

  double wall[3], prod[3], cons[3], tp[3];

  /* 開啟兩個 character devices；後續測試共用這些 fd。 */
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

  /* Test 1：MQ，資料路徑為 write/read syscall + kfifo。 */
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

  /* Test 2：SHM syscall，底層是 ring，但每筆仍走 write/read。 */
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

  /* Test 3：SHM mmap，重設 ring 後直接用 shared memory 傳資料。 */
  shm->head.value = shm->tail.value = 0; /* 讓 mmap 測試從空 ring 開始。 */
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

  /* 總結：以 MQ throughput 當 baseline，顯示其他路徑的倍率。 */
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
