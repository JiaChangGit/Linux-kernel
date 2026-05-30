#include "ftl.h"
#include "gc.h"

ftl_context_t g_ftl;

static bool ftl_request_range_is_valid(const ftl_context_t *ftl,
                                       const request_t *request,
                                       uint32_t *start_lpn,
                                       uint32_t *end_lpn)
{
    uint64_t last_lpn_exclusive;

    /* 長度為 0 的 request 視為 no-op，不會碰 mapping table。 */
    if (request->length == 0) {
        *start_lpn = 0;
        *end_lpn = 0;
        return true;
    }

    last_lpn_exclusive = request->lba + (uint64_t)request->length;
    /* 同時檢查加法 overflow 與 logical range，避免 L2P 越界。 */
    if (last_lpn_exclusive < request->lba ||
        request->lba >= ftl->config->logical_pages ||
        last_lpn_exclusive > ftl->config->logical_pages) {
        LOG_ERROR("Request out of logical range, lba=%llu length=%u logical_pages=%u",
                  (unsigned long long)request->lba,
                  request->length,
                  ftl->config->logical_pages);
        return false;
    }

    *start_lpn = (uint32_t)request->lba;
    *end_lpn = (uint32_t)last_lpn_exclusive;
    return true;
}

static bool ftl_handle_write(ftl_context_t *ftl, const request_t *request)
{
    uint32_t start_lpn = 0;
    uint32_t end_lpn = 0;
    bool sequential_request = false;

    if (!ftl_request_range_is_valid(ftl, request, &start_lpn, &end_lpn)) {
        return false;
    }

    for (uint32_t lpn = start_lpn; lpn < end_lpn; lpn++) {
        physical_page_address_t old_ppa;
        physical_page_address_t new_ppa;
        bool has_old = mapping_get_physical_page(ftl->mapping_table, lpn, &old_ppa);

        /* 寫入前先做預防性 GC，避免長 request 吃掉 GC migration 需要的空間。 */
        if (gc_needed(ftl)) {
            (void)gc_run(ftl, false);
        }

        if (!nand_allocate_page(&ftl->nand,
                                &ftl->free_block_pool,
                                &ftl->current_write_block,
                                &new_ppa)) {
            if (!gc_run(ftl, true)) {
                return false;
            }

            if (!nand_allocate_page(&ftl->nand,
                                    &ftl->free_block_pool,
                                    &ftl->current_write_block,
                                    &new_ppa)) {
                LOG_ERROR("%s", "Out of NAND space");
                return false;
            }
        }

        /* 新 page program 成功後，舊 page 才能標成 invalid。 */
        nand_program_page(&ftl->nand, &new_ppa, lpn);
        ftl->current_time_us += ftl->config->program_latency_us;

        if (has_old) {
            nand_invalidate_page(&ftl->nand, &old_ppa);
        }

        mapping_set_physical_page(ftl->mapping_table, lpn, &new_ppa);
        ftl->lpn_write_count[lpn]++;

        ftl->stats.host_page_count++;
        ftl->stats.nand_write_count++;

        LOG_DEBUG("WRITE LPN=%u -> PPA=(%u,%u)",
                  lpn,
                  new_ppa.block_index,
                  new_ppa.page_index);
    }

    if (request->length == 0) {
        return true;
    }

    /* Sequential 判斷採 request-level：本次起點等於上一筆結尾。 */
    sequential_request = ftl->has_last_write &&
                         request->lba == ftl->last_write_end_lpn;

    if (sequential_request) {
        ftl->stats.sequential_write_count++;
    } else {
        ftl->stats.random_write_count++;
    }

    ftl->last_write_end_lpn = request->lba + (uint64_t)request->length;
    ftl->has_last_write = true;

    return true;
}

int ftl_init(const ssd_config_t *config)
{
    g_ftl.config = config;

    if (nand_init(&g_ftl.nand, config) != 0) {
        return -1;
    }

    /* L2P table 大小等於 logical_pages，每個 LPN 對應一筆 entry。 */
    g_ftl.mapping_table = calloc(config->logical_pages, sizeof(mapping_entry_t));
    if (!g_ftl.mapping_table) {
        nand_destroy(&g_ftl.nand);
        return -1;
    }
    mapping_table_init(g_ftl.mapping_table, config->logical_pages);

    g_ftl.lpn_write_count = calloc(config->logical_pages, sizeof(uint32_t));
    if (!g_ftl.lpn_write_count) {
        free(g_ftl.mapping_table);
        g_ftl.mapping_table = NULL;
        nand_destroy(&g_ftl.nand);
        return -1;
    }

    if (free_block_pool_init(&g_ftl.free_block_pool, config->total_blocks) != 0) {
        free(g_ftl.lpn_write_count);
        free(g_ftl.mapping_table);
        g_ftl.lpn_write_count = NULL;
        g_ftl.mapping_table = NULL;
        nand_destroy(&g_ftl.nand);
        return -1;
    }

    /* 初始時所有 block 都可用，先放進 pool，再取一個當 current_write_block。 */
    for (uint32_t block = 0; block < config->total_blocks; block++) {
        if (!free_block_pool_push(&g_ftl.free_block_pool, block)) {
            ftl_destroy();
            return -1;
        }
    }

    if (!free_block_pool_pop(&g_ftl.free_block_pool, &g_ftl.current_write_block)) {
        ftl_destroy();
        return -1;
    }

    g_ftl.current_time_us = 0;
    g_ftl.last_write_end_lpn = 0;
    g_ftl.gc_stall_active = false;
    g_ftl.has_last_write = false;
    stats_init(&g_ftl.stats);
    return 0;
}

void ftl_destroy(void)
{
    free_block_pool_destroy(&g_ftl.free_block_pool);
    free(g_ftl.lpn_write_count);
    free(g_ftl.mapping_table);
    g_ftl.lpn_write_count = NULL;
    g_ftl.mapping_table = NULL;
    nand_destroy(&g_ftl.nand);
}

bool ftl_handle_request(ftl_context_t *ftl, const request_t *request)
{
    switch (request->type) {
    case REQUEST_TYPE_WRITE:
        return ftl_handle_write(ftl, request);
    default:
        return false;
    }
}
