#ifndef FTL_H
#define FTL_H

#include "config.h"
#include "mapping.h"
#include "nand.h"
#include "request.h"
#include "stats.h"

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

extern ftl_context_t g_ftl;

int ftl_init(const ssd_config_t *config);
void ftl_destroy(void);
bool ftl_handle_request(ftl_context_t *ftl, const request_t *request);

#endif
