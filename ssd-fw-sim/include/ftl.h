#ifndef FTL_H
#define FTL_H

#include "config.h"
#include "mapping.h"
#include "nand.h"
#include "request.h"
#include "stats.h"

/* FTL 的主要狀態集合；負責串起 mapping、NAND、GC 與統計。 */
typedef struct {
    const ssd_config_t *config;
    nand_ssd_t nand;
    mapping_entry_t *mapping_table;
    uint32_t *lpn_write_count;
    free_block_pool_t free_block_pool;
    uint32_t current_write_block;
    uint64_t current_time_us;
    uint64_t last_write_end_lpn;
    bool gc_stall_active;
    bool has_last_write;
    ssd_statistics_t stats;
} ftl_context_t;

/* 模擬器目前使用單一全域 FTL instance。 */
extern ftl_context_t g_ftl;

int ftl_init(const ssd_config_t *config);
void ftl_destroy(void);
/* FTL 入口：scheduler 只需要丟 request，不需要知道 NAND 細節。 */
bool ftl_handle_request(ftl_context_t *ftl, const request_t *request);

#endif
