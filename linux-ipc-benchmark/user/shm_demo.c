/*
 * shm_demo.c - Shared Memory mmap 操作示範
 *
 * 流程：
 *   1. 開啟 /dev/shm_ipc。
 *   2. mmap() kernel 端的 shared ring buffer。
 *   3. producer 直接寫入 shm->data[head]。
 *   4. consumer 直接讀取 shm->data[tail]。
 *
 * 這條路徑每筆訊息不呼叫 read/write，也不經 copy_from_user/copy_to_user。
 * 執行前需先載入 shm_module.ko，可用 scripts/02_demo.sh 一起操作。
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "common.h"

#define DEMO_N 8

int main(void) {
  int fd;
  shm_region_t* shm;
  int i;
  uint32_t head, tail, next;
  double t0, t1;

  printf("┌──────────────────────────────────────────────────────────┐\n");
  printf("│       Shared Memory Demo   (/dev/shm_ipc + mmap)         │\n");
  printf("├──────────────────────────────────────────────────────────┤\n");
  printf("│  mmap() → both sides share the SAME physical pages       │\n");
  printf("│  producer writes data[head] directly — ZERO extra copy   │\n");
  printf("│  consumer reads data[tail] directly — ZERO extra copy    │\n");
  printf("└──────────────────────────────────────────────────────────┘\n\n");

  fd = open(SHM_DEVICE, O_RDWR);
  if (fd < 0) {
    perror("open " SHM_DEVICE);
    return 1;
  }

  shm = mmap(NULL, SHM_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (shm == MAP_FAILED) {
    perror("mmap");
    close(fd);
    return 1;
  }

  /* demo 每次都從空 ring 開始，避免受到前一次測試殘留狀態影響。 */
  shm->head.value = 0;
  shm->tail.value = 0;
  printf("  mmap OK  →  userspace ptr %p  maps kernel ring-buffer\n",
         (void*)shm);
  printf("  SHM_MAP_SIZE = %lu bytes  (%lu pages)\n\n", SHM_MAP_SIZE,
         SHM_MAP_SIZE / 4096);

  printf("[Producer]  Write %d messages directly into shared pages\n\n",
         DEMO_N);
  for (i = 0; i < DEMO_N; i++) {
    /* demo 訊息數很少，通常不會滿；保留 full 判斷讓 ring 邏輯完整。 */
    do {
      head = shm->head.value;
      next = (head + 1) % RING_CAPACITY;
    } while (next == shm->tail.value);

    t0 = now_us();
    snprintf(shm->data[head], MSG_SIZE, "SHM-MSG[%02d] slot=%-3u data=%08x", i,
             head, i * 0xDEAD);
    __sync_synchronize(); /* write barrier：先讓資料可見，再更新 head。 */
    shm->head.value = next;
    t1 = now_us();
    printf("  prod[%02d]  slot[%03u]  %-30s  Δ=%.1f µs\n", i, head,
           shm->data[head], t1 - t0);
  }

  printf("\n[Consumer]  Read %d messages directly from shared pages\n\n",
         DEMO_N);
  for (i = 0; i < DEMO_N; i++) {
    /* 若 consumer 追上 producer，就等待 head 前進。 */
    do {
      tail = shm->tail.value;
    } while (tail == shm->head.value);

    t0 = now_us();
    __sync_synchronize(); /* read barrier：先看見 head 更新，再讀資料。 */
    /* 資料已在本行程的位址空間內，這裡只是複製一份快照方便列印。 */
    char snapshot[MSG_SIZE];
    memcpy(snapshot, shm->data[tail], MSG_SIZE);
    shm->tail.value = (tail + 1) % RING_CAPACITY;
    t1 = now_us();
    snapshot[MSG_SIZE - 1] = '\0';
    printf("  cons[%02d]  slot[%03u]  %-30s  Δ=%.1f µs\n", i, tail, snapshot,
           t1 - t0);
  }

  printf("\n[/proc/shm_stats]\n");
  system("cat /proc/shm_stats | sed 's/^/  /'");

  munmap(shm, SHM_MAP_SIZE);
  close(fd);
  return 0;
}
