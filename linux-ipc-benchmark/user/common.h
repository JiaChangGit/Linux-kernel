/*
 * common.h  —  constants and types shared by all userspace programs
 *
 * The shm_region_t layout MUST mirror struct shm_region in kernel/shm_module.c.
 * If you change MSG_SIZE or RING_CAPACITY in the kernel, update here too.
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <time.h>

/* ── tunable constants (mirror kernel values) ────────────────────────── */
#define MSG_SIZE        64
#define RING_CAPACITY   512

#define MQ_DEVICE   "/dev/mq_ipc"
#define SHM_DEVICE  "/dev/shm_ipc"

/*
 * shm_region_t  —  ring-buffer layout mapped from /dev/shm_ipc
 *
 * head : next write slot  (producer owns; only producer writes this)
 * tail : next read  slot  (consumer owns; only consumer writes this)
 *
 * Empty : head == tail
 * Full  : (head + 1) % RING_CAPACITY == tail
 */
typedef struct {
    volatile uint32_t value;
    uint8_t padding[64 - sizeof(uint32_t)];
} cacheline_u32_t;

typedef struct __attribute__((aligned(64))) {

    cacheline_u32_t head;
    cacheline_u32_t tail;

    struct {
        uint32_t capacity;
        uint32_t msg_size;
        uint8_t padding[64 - 2 * sizeof(uint32_t)];
    } meta;

    char data[RING_CAPACITY][MSG_SIZE];

} shm_region_t;

/* mmap size — must equal SHM_BUF_SIZE in shm_module.c */
#define SHM_MAP_SIZE   (((sizeof(shm_region_t) + 4095UL)) & ~4095UL)

/* ── timing helper ───────────────────────────────────────────────────── */
static inline double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec * 1e-3;
}

#endif /* COMMON_H */
