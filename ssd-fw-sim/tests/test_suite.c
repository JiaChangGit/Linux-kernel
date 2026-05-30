#include "config.h"
#include "ftl.h"
#include "mapping.h"
#include "nand.h"
#include "nvme.h"
#include "request.h"
#include "scheduler.h"

#include <assert.h>

static bool service_pipeline(ftl_context_t *ftl,
                             nvme_controller_t *controller,
                             request_queue_t *request_queue)
{
    /* 測試版 pipeline 與 main.c 相同：SQ -> RQ -> scheduler -> CQ reap。 */
    (void)nvme_issue_pending(controller, request_queue);

    if (!request_queue_is_empty(request_queue) &&
        !scheduler_run(ftl, request_queue, controller)) {
        return false;
    }

    (void)nvme_reap_completions(controller);
    return true;
}

static void init_small_config(ssd_config_t *config)
{
    /* 小容量設定可快速觸發 GC，測試也比較容易看出邊界條件。 */
    ssd_config_init_default(config);
    config->total_blocks = 8;
    config->pages_per_block = 4;
    config->logical_pages = 24;
    config->request_queue_depth = 16;
    config->gc_free_block_threshold = 2;
    config->read_latency_us = 50;
    config->program_latency_us = 200;
    config->erase_latency_us = 1500;
    config->trace_inter_arrival_us = 10;
    assert(ssd_config_validate(config) == 0);
}

static log_level_t disable_logs(void)
{
    /* 預期會失敗的測試先關 log，避免錯誤訊息干擾測試輸出。 */
    log_level_t previous_log_level = g_log_level;
    g_log_level = LOG_LEVEL_NONE;
    return previous_log_level;
}

static void restore_logs(log_level_t previous_log_level)
{
    g_log_level = previous_log_level;
}

static void assert_latency_accounting_consistent(const ssd_statistics_t *stats)
{
    /* Queue latency 與 service latency 應可加回 total latency。 */
    assert(stats->total_queue_latency_us + stats->total_service_latency_us ==
           stats->total_latency_us);
}

static void write_test_file(const char *path, const char *contents)
{
    FILE *fp = fopen(path, "w");

    assert(fp != NULL);
    assert(fputs(contents, fp) >= 0);
    fclose(fp);
}

static void test_config_rejects_impossible_geometry(void)
{
    ssd_config_t config;
    log_level_t previous_log_level;

    /* logical_pages 超過實體容量時，FTL mapping 會沒有合法 PPA 可指。 */
    ssd_config_init_default(&config);
    config.total_blocks = 2;
    config.pages_per_block = 4;
    config.logical_pages = 9;

    previous_log_level = disable_logs();
    assert(ssd_config_validate(&config) != 0);
    restore_logs(previous_log_level);

    /* GC threshold 為 0 會讓預防性 GC 失去意義。 */
    ssd_config_init_default(&config);
    config.gc_free_block_threshold = 0;

    previous_log_level = disable_logs();
    assert(ssd_config_validate(&config) != 0);
    restore_logs(previous_log_level);
}

static void test_config_load_rejects_malformed_entries(void)
{
    const char *bad_value_path = "build/tests/test_bad_value.conf";
    const char *unknown_key_path = "build/tests/test_unknown_key.conf";
    ssd_config_t config;
    log_level_t previous_log_level;

    /* 數值欄位必須是純數字，單位請由 key 名稱表達。 */
    write_test_file(bad_value_path, "program_latency_us=200us\n");
    ssd_config_init_default(&config);

    previous_log_level = disable_logs();
    assert(ssd_config_load_file(bad_value_path, &config) != 0);
    restore_logs(previous_log_level);
    assert(remove(bad_value_path) == 0);

    /* 未知 key 直接失敗，避免使用者誤以為設定已生效。 */
    write_test_file(unknown_key_path, "program_latency_us=200\nmystery_key=1\n");
    ssd_config_init_default(&config);

    previous_log_level = disable_logs();
    assert(ssd_config_load_file(unknown_key_path, &config) != 0);
    restore_logs(previous_log_level);
    assert(remove(unknown_key_path) == 0);
}

static void test_nvme_sq_cq_lifecycle(void)
{
    nvme_controller_t controller;
    request_queue_t request_queue;
    request_t request;

    assert(nvme_controller_init(&controller, 2) == 0);
    assert(request_queue_init(&request_queue, 2) == 0);

    /* 深度為 2 時，第三筆 submit 應失敗，驗證 SQ full 行為。 */
    assert(nvme_submit_write(&controller, 10, 2, 0) == 0);
    assert(nvme_submit_write(&controller, 20, 4, 5) == 0);
    assert(nvme_submit_write(&controller, 30, 1, 10) != 0);
    assert(nvme_sq_count(&controller) == 2);

    assert(nvme_issue_pending(&controller, &request_queue) == 2);
    assert(nvme_sq_is_empty(&controller));

    /* issue 後 command_id、LBA 與 length 必須完整保留到 request_t。 */
    assert(request_queue_dequeue(&request_queue, &request));
    assert(request.command_id == 0);
    assert(request.queue_id == 1);
    assert(request.lba == 10);
    assert(request.length == 2);

    assert(nvme_post_completion(&controller, &request, NVME_STATUS_SUCCESS, 100));

    assert(request_queue_dequeue(&request_queue, &request));
    assert(request.command_id == 1);
    assert(request.lba == 20);
    assert(request.length == 4);
    assert(nvme_post_completion(&controller, &request, NVME_STATUS_SUCCESS, 150));

    assert(nvme_cq_is_full(&controller));
    /* Host reap completion 後，CQ slot 應全部釋放。 */
    assert(nvme_reap_completions(&controller) == 2);
    assert(nvme_cq_is_empty(&controller));

    request_queue_destroy(&request_queue);
    nvme_controller_destroy(&controller);
}

static void test_scheduler_pipeline_posts_completions(void)
{
    ssd_config_t config;
    nvme_controller_t controller;
    request_queue_t request_queue;

    init_small_config(&config);
    assert(ftl_init(&config) == 0);
    assert(nvme_controller_init(&controller, config.request_queue_depth) == 0);
    assert(request_queue_init(&request_queue, config.request_queue_depth) == 0);

    assert(nvme_submit_write(&controller, 0, 4, 0) == 0);
    assert(nvme_submit_write(&controller, 4, 2, 10) == 0);
    assert(service_pipeline(&g_ftl, &controller, &request_queue));

    /* 兩筆 host request 共寫 6 pages，沒有 GC 時 NAND writes 也應為 6。 */
    assert(g_ftl.stats.host_request_count == 2);
    assert(g_ftl.stats.host_page_count == 6);
    assert(g_ftl.stats.nand_write_count == 6);
    assert(controller.submission_count == 2);
    assert(controller.dispatch_count == 2);
    assert(controller.completion_count == 2);
    assert(controller.reaped_completion_count == 2);
    assert_latency_accounting_consistent(&g_ftl.stats);

    request_queue_destroy(&request_queue);
    nvme_controller_destroy(&controller);
    ftl_destroy();
}

static void test_ftl_rejects_out_of_range_write(void)
{
    ssd_config_t config;
    log_level_t previous_log_level;
    request_t request = {
        .type = REQUEST_TYPE_WRITE,
        .command_id = 7,
        .queue_id = 1,
        .lba = 24,
        .length = 1,
        .submit_timestamp_us = 0,
    };

    init_small_config(&config);
    assert(ftl_init(&config) == 0);

    previous_log_level = disable_logs();
    assert(!ftl_handle_request(&g_ftl, &request));
    restore_logs(previous_log_level);
    assert(g_ftl.stats.host_page_count == 0);
    assert(g_ftl.stats.nand_write_count == 0);

    ftl_destroy();
}

static void assert_mapping_is_live(const ftl_context_t *ftl, uint32_t lpn)
{
    physical_page_address_t ppa;
    const nand_page_t *page;

    /* L2P 查到的 PPA 必須仍指向有效 page，這是 FTL 正確性的核心。 */
    assert(mapping_get_physical_page(ftl->mapping_table, lpn, &ppa));
    page = &ftl->nand.blocks[ppa.block_index].pages[ppa.page_index];
    assert(page->state == NAND_PAGE_VALID);
    assert(page->has_logical_page);
    assert(page->logical_page_number == lpn);
}

static void test_gc_preserves_valid_mappings(void)
{
    ssd_config_t config;
    nvme_controller_t controller;
    request_queue_t request_queue;
    const request_t requests[] = {
        { .type = REQUEST_TYPE_WRITE, .command_id = 0, .queue_id = 1, .lba = 0,  .length = 4, .submit_timestamp_us = 0 },
        { .type = REQUEST_TYPE_WRITE, .command_id = 1, .queue_id = 1, .lba = 4,  .length = 4, .submit_timestamp_us = 10 },
        { .type = REQUEST_TYPE_WRITE, .command_id = 2, .queue_id = 1, .lba = 8,  .length = 4, .submit_timestamp_us = 20 },
        { .type = REQUEST_TYPE_WRITE, .command_id = 3, .queue_id = 1, .lba = 12, .length = 1, .submit_timestamp_us = 30 },
        { .type = REQUEST_TYPE_WRITE, .command_id = 4, .queue_id = 1, .lba = 12, .length = 1, .submit_timestamp_us = 40 },
        { .type = REQUEST_TYPE_WRITE, .command_id = 5, .queue_id = 1, .lba = 13, .length = 1, .submit_timestamp_us = 50 },
        { .type = REQUEST_TYPE_WRITE, .command_id = 6, .queue_id = 1, .lba = 14, .length = 1, .submit_timestamp_us = 60 },
        { .type = REQUEST_TYPE_WRITE, .command_id = 7, .queue_id = 1, .lba = 15, .length = 1, .submit_timestamp_us = 70 },
        { .type = REQUEST_TYPE_WRITE, .command_id = 8, .queue_id = 1, .lba = 13, .length = 1, .submit_timestamp_us = 80 },
    };

    init_small_config(&config);
    /* 這組 geometry 會讓覆寫產生 invalid page，進而觸發 GC。 */
    config.total_blocks = 6;
    config.pages_per_block = 4;
    config.logical_pages = 20;
    assert(ssd_config_validate(&config) == 0);

    assert(ftl_init(&config) == 0);
    assert(nvme_controller_init(&controller, config.request_queue_depth) == 0);
    assert(request_queue_init(&request_queue, config.request_queue_depth) == 0);

    for (size_t i = 0; i < (sizeof(requests) / sizeof(requests[0])); i++) {
        assert(nvme_submit_write(&controller,
                                 requests[i].lba,
                                 requests[i].length,
                                 requests[i].submit_timestamp_us) == 0);
    }

    assert(nvme_issue_pending(&controller, &request_queue) == (sizeof(requests) / sizeof(requests[0])));
    assert(scheduler_run(&g_ftl, &request_queue, &controller));
    assert(g_ftl.stats.gc_count > 0);
    assert_latency_accounting_consistent(&g_ftl.stats);

    for (uint32_t lpn = 0; lpn <= 15; lpn++) {
        assert_mapping_is_live(&g_ftl, lpn);
    }

    assert(nvme_reap_completions(&controller) == (sizeof(requests) / sizeof(requests[0])));

    request_queue_destroy(&request_queue);
    nvme_controller_destroy(&controller);
    ftl_destroy();
}

static void test_long_request_preserves_gc_migration_space(void)
{
    ssd_config_t config;
    nvme_controller_t controller;
    request_queue_t request_queue;
    const request_t requests[] = {
        { .lba = 0, .length = 4, .submit_timestamp_us = 0 },
        { .lba = 4, .length = 4, .submit_timestamp_us = 10 },
        { .lba = 0, .length = 1, .submit_timestamp_us = 20 },
        { .lba = 8, .length = 8, .submit_timestamp_us = 30 },
    };

    init_small_config(&config);
    /* 小容量加長 request，用來驗證寫入中途會預先保留 GC 搬移空間。 */
    config.total_blocks = 4;
    config.pages_per_block = 4;
    config.logical_pages = 16;
    config.gc_free_block_threshold = 1;
    assert(ssd_config_validate(&config) == 0);

    assert(ftl_init(&config) == 0);
    assert(nvme_controller_init(&controller, config.request_queue_depth) == 0);
    assert(request_queue_init(&request_queue, config.request_queue_depth) == 0);

    for (size_t i = 0; i < (sizeof(requests) / sizeof(requests[0])); i++) {
        assert(nvme_submit_write(&controller,
                                 requests[i].lba,
                                 requests[i].length,
                                 requests[i].submit_timestamp_us) == 0);
    }

    assert(nvme_issue_pending(&controller, &request_queue) == (sizeof(requests) / sizeof(requests[0])));
    assert(scheduler_run(&g_ftl, &request_queue, &controller));
    assert(g_ftl.stats.gc_count > 0);
    assert(g_ftl.stats.background_gc_count > 0);
    assert_latency_accounting_consistent(&g_ftl.stats);

    for (uint32_t lpn = 0; lpn < 16; lpn++) {
        assert_mapping_is_live(&g_ftl, lpn);
    }

    assert(nvme_reap_completions(&controller) == (sizeof(requests) / sizeof(requests[0])));

    request_queue_destroy(&request_queue);
    nvme_controller_destroy(&controller);
    ftl_destroy();
}

int main(void)
{
    g_log_level = LOG_LEVEL_ERROR;

    test_config_rejects_impossible_geometry();
    test_config_load_rejects_malformed_entries();
    test_nvme_sq_cq_lifecycle();
    test_scheduler_pipeline_posts_completions();
    test_ftl_rejects_out_of_range_write();
    test_gc_preserves_valid_mappings();
    test_long_request_preserves_gc_migration_space();

    printf("All tests passed\n");
    return 0;
}
