/*
 * common.h - user-space 程式共用的常數與型別
 *
 * MSG_SIZE、RING_CAPACITY 與 shm_region_t 會影響 mmap ABI。
 * 只要 kernel/shm_module.c 的 struct shm_region 有調整，這裡也要同步檢查。
 * 特別注意 data 欄位 offset；kernel 與 user 對同一段 shared memory
 * 必須有相同解讀，否則 mmap path 可能讀寫到錯誤位置。
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <time.h>

/* ── 與 kernel module 對應的固定參數 ───────────────────────────────── */
#define MSG_SIZE        64
#define RING_CAPACITY   512

#define MQ_DEVICE   "/dev/mq_ipc"
#define SHM_DEVICE  "/dev/shm_ipc"

/*
 * shm_region_t - /dev/shm_ipc mmap 後在 user space 看到的 ring layout。
 *
 * head：下一個可寫入 slot，由 producer 更新。
 * tail：下一個可讀取 slot，由 consumer 更新。
 *
 * Empty：head == tail。
 * Full ：(head + 1) % RING_CAPACITY == tail。
 *
 * cacheline_u32_t 讓 head/tail 各自佔一條 cache line，降低 false sharing。
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

/* mmap 長度需對齊 page；應與 kernel 端 SHM_BUF_SIZE 保持一致。 */
#define SHM_MAP_SIZE   (((sizeof(shm_region_t) + 4095UL)) & ~4095UL)

/* ── 時間量測工具：回傳 monotonic clock 的微秒值 ─────────────────── */
static inline double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec * 1e-3;
}

#endif /* COMMON_H */
