/*
 * shm_demo.c  —  Shared Memory (mmap zero-copy) demonstration
 *
 * Opens /dev/shm_ipc, mmaps the kernel ring-buffer, then writes and reads
 * directly from the mapped pages — no copy_from/to_user per message.
 *
 * Run via scripts/02_demo.sh  (modules must be loaded first).
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "common.h"

#define DEMO_N  8

int main(void)
{
    int          fd;
    shm_region_t *shm;
    int          i;
    uint32_t     head, tail, next;
    double       t0, t1;

    printf("┌──────────────────────────────────────────────────────────┐\n");
    printf("│       Shared Memory Demo   (/dev/shm_ipc + mmap)         │\n");
    printf("├──────────────────────────────────────────────────────────┤\n");
    printf("│  mmap() → both sides share the SAME physical pages       │\n");
    printf("│  producer writes data[head] directly — ZERO extra copy   │\n");
    printf("│  consumer reads data[tail] directly — ZERO extra copy    │\n");
    printf("└──────────────────────────────────────────────────────────┘\n\n");

    fd = open(SHM_DEVICE, O_RDWR);
    if (fd < 0) { perror("open " SHM_DEVICE); return 1; }

    shm = mmap(NULL, SHM_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    /* reset ring (demo always starts fresh) */
    shm->head = 0;
    shm->tail = 0;
    printf("  mmap OK  →  userspace ptr %p  maps kernel ring-buffer\n",
           (void *)shm);
    printf("  SHM_MAP_SIZE = %lu bytes  (%lu pages)\n\n",
           SHM_MAP_SIZE, SHM_MAP_SIZE / 4096);

    printf("[Producer]  Write %d messages directly into shared pages\n\n", DEMO_N);
    for (i = 0; i < DEMO_N; i++) {
        /* spin-wait if full (unlikely in demo) */
        do {
            head = shm->head;
            next = (head + 1) % RING_CAPACITY;
        } while (next == shm->tail);

        t0 = now_us();
        snprintf(shm->data[head], MSG_SIZE,
                 "SHM-MSG[%02d] slot=%-3u data=%08x", i, head, i * 0xDEAD);
        __sync_synchronize();      /* wmb: data visible before head bump  */
        shm->head = next;
        t1 = now_us();
        printf("  prod[%02d]  slot[%03u]  %-30s  Δ=%.1f µs\n",
               i, head, shm->data[head], t1 - t0);
    }

    printf("\n[Consumer]  Read %d messages directly from shared pages\n\n", DEMO_N);
    for (i = 0; i < DEMO_N; i++) {
        /* spin-wait if empty (unlikely in demo) */
        do { tail = shm->tail; } while (tail == shm->head);

        t0 = now_us();
        __sync_synchronize();      /* rmb: see head update before data    */
        /* read in-place — the data IS already in our address space       */
        char snapshot[MSG_SIZE];
        memcpy(snapshot, shm->data[tail], MSG_SIZE);
        shm->tail = (tail + 1) % RING_CAPACITY;
        t1 = now_us();
        snapshot[MSG_SIZE-1] = '\0';
        printf("  cons[%02d]  slot[%03u]  %-30s  Δ=%.1f µs\n",
               i, tail, snapshot, t1 - t0);
    }

    printf("\n[/proc/shm_stats]\n");
    system("cat /proc/shm_stats | sed 's/^/  /'");

    munmap(shm, SHM_MAP_SIZE);
    close(fd);
    return 0;
}
