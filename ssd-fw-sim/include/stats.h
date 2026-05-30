#ifndef STATS_H
#define STATS_H

#include "common.h"

/* 模擬器統計資料；計數與 latency 分開保存，方便分析瓶頸。 */
typedef struct {
    uint64_t host_request_count;
    uint64_t host_page_count;
    uint64_t nand_write_count;
    uint64_t nand_read_count;
    uint64_t nand_erase_count;
    uint64_t gc_count;
    uint64_t migrated_page_count;
    uint64_t foreground_gc_count;
    uint64_t background_gc_count;
    uint64_t sequential_write_count;
    uint64_t random_write_count;
    uint64_t total_queue_latency_us;
    uint64_t total_service_latency_us;
    uint64_t total_latency_us;
    uint64_t max_queue_latency_us;
    uint64_t max_service_latency_us;
    uint64_t max_latency_us;
} ssd_statistics_t;

void stats_init(ssd_statistics_t *stats);
/* WA = NAND 實際寫入頁數 / Host 要求寫入頁數。 */
double stats_write_amplification(const ssd_statistics_t *stats);
uint64_t stats_average_latency_us(const ssd_statistics_t *stats);
/* 每筆 request 完成時呼叫一次，集中更新 latency 累積值與最大值。 */
void stats_update_request(ssd_statistics_t *stats,
                          uint64_t queue_latency_us,
                          uint64_t service_latency_us,
                          uint64_t total_latency_us);
void stats_print(const ssd_statistics_t *stats);
int stats_export_csv(const ssd_statistics_t *stats, const char *path);

#endif
