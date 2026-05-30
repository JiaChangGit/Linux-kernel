#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

/* SSD 幾何、佇列深度與 NAND 延遲設定。 */
typedef struct ssd_config {
    /* 實體 NAND block 數量。 */
    uint32_t total_blocks;
    /* 每個 block 內的 page 數量。 */
    uint32_t pages_per_block;
    /* Host 可看到的邏輯 page 數，不能超過實體 page 總數。 */
    uint32_t logical_pages;
    /* 本模擬器同時用於 SQ、CQ 與內部 request queue 的深度。 */
    uint32_t request_queue_depth;
    /* Free block pool 低於此門檻時觸發預防性 GC。 */
    uint32_t gc_free_block_threshold;
    /* NAND read / program / erase 的模擬延遲，單位為微秒。 */
    uint32_t read_latency_us;
    uint32_t program_latency_us;
    uint32_t erase_latency_us;
    /* Trace 中相鄰 request 的模擬抵達間隔，單位為微秒。 */
    uint32_t trace_inter_arrival_us;
} ssd_config_t;

extern ssd_config_t g_config;

/* 先載入預設值，再依需求用設定檔覆寫。 */
void ssd_config_init_default(ssd_config_t *config);
int ssd_config_load_file(const char *path, ssd_config_t *config);
/* 所有初始化完成前都應先呼叫 validate，避免後續配置非法容量。 */
int ssd_config_validate(const ssd_config_t *config);
void ssd_config_print(const ssd_config_t *config);

#endif
