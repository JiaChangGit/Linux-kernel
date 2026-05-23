#include "common.h"
#include "config.h"
#include "ftl.h"
#include "nvme.h"
#include "request.h"
#include "scheduler.h"
#include "stats.h"
#include <inttypes.h>

static bool trace_request_is_in_range(const ftl_context_t *ftl, uint64_t lba, uint32_t size)
{
    uint64_t end_lpn_exclusive;

    if (size == 0) {
        return true;
    }

    end_lpn_exclusive = lba + (uint64_t)size;
    return end_lpn_exclusive >= lba &&
           lba < ftl->config->logical_pages &&
           end_lpn_exclusive <= ftl->config->logical_pages;
}

static bool service_nvme_pipeline(ftl_context_t *ftl,
                                  nvme_controller_t *controller,
                                  request_queue_t *request_queue)
{
    (void)nvme_issue_pending(controller, request_queue);

    if (!request_queue_is_empty(request_queue) &&
        !scheduler_run(ftl, request_queue, controller)) {
        return false;
    }

    (void)nvme_reap_completions(controller);
    return true;
}

static int replay_trace(const char *trace_path,
                        nvme_controller_t *controller,
                        request_queue_t *request_queue,
                        uint64_t inter_arrival_us,
                        ftl_context_t *ftl)
{
    FILE *fp = fopen(trace_path, "r");
    char line[256];
    uint64_t submit_timestamp_us = 0;

    if (!fp) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char op[16];
        uint64_t lba = 0;
        uint32_t size = 0;

        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        if (sscanf(line, "%15s %" SCNu64 " %u", op, &lba, &size) != 3) {
            continue;
        }

        if (strcmp(op, "WRITE") != 0) {
            continue;
        }

        if (!trace_request_is_in_range(ftl, lba, size)) {
            LOG_ERROR("Trace request out of logical range, lba=%llu size=%u logical_pages=%u",
                      (unsigned long long)lba,
                      size,
                      ftl->config->logical_pages);
            fclose(fp);
            return -1;
        }

        if (nvme_submit_write(controller, lba, size, submit_timestamp_us) != 0) {
            if (!service_nvme_pipeline(ftl, controller, request_queue)) {
                fclose(fp);
                return -1;
            }

            if (nvme_submit_write(controller, lba, size, submit_timestamp_us) != 0) {
                LOG_ERROR("NVMe submission queue enqueue failed after draining, lba=%llu size=%u",
                          (unsigned long long)lba,
                          size);
                fclose(fp);
                return -1;
            }
        }

        if (!service_nvme_pipeline(ftl, controller, request_queue)) {
            fclose(fp);
            return -1;
        }

        if (nvme_cq_is_full(controller)) {
            if (nvme_reap_completions(controller) == 0) {
                fclose(fp);
                return -1;
            }
        }

        submit_timestamp_us += inter_arrival_us;
    }

    fclose(fp);
    return 0;
}

int main(int argc, char **argv)
{
    const char *trace_path = NULL;
    const char *config_path = NULL;
    const char *csv_path = NULL;
    ssd_config_t config;
    request_queue_t request_queue;
    nvme_controller_t nvme;
    int rc = 0;

    ssd_config_init_default(&config);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (!trace_path) {
            trace_path = argv[i];
        } else {
            LOG_WARN("Ignoring extra argument: %s", argv[i]);
        }
    }

    if (!trace_path) {
        fprintf(stderr, "Usage: %s [--config ssd.conf] [--csv out.csv] <trace>\n", argv[0]);
        return 1;
    }

    if (config_path) {
        if (ssd_config_load_file(config_path, &config) != 0) {
            fprintf(stderr, "Failed to load config: %s\n", config_path);
            return 1;
        }
    }

    if (ssd_config_validate(&config) != 0) {
        fprintf(stderr, "Invalid SSD configuration\n");
        return 1;
    }

    g_config = config;
    ssd_config_print(&config);

    if (request_queue_init(&request_queue, config.request_queue_depth) != 0) {
        fprintf(stderr, "Failed to initialize request queue\n");
        return 1;
    }

    if (nvme_controller_init(&nvme, config.request_queue_depth) != 0) {
        fprintf(stderr, "Failed to initialize NVMe controller queues\n");
        request_queue_destroy(&request_queue);
        return 1;
    }

    if (ftl_init(&config) != 0) {
        fprintf(stderr, "Failed to initialize FTL\n");
        nvme_controller_destroy(&nvme);
        request_queue_destroy(&request_queue);
        return 1;
    }

    if (replay_trace(trace_path,
                     &nvme,
                     &request_queue,
                     config.trace_inter_arrival_us,
                     &g_ftl) != 0) {
        fprintf(stderr, "Trace replay failed: %s\n", trace_path);
        rc = 1;
        goto out;
    }

    while (nvme_has_pending(&nvme, &request_queue)) {
        if (!service_nvme_pipeline(&g_ftl, &nvme, &request_queue)) {
            rc = 1;
            goto out;
        }
    }

    stats_print(&g_ftl.stats);

    if (csv_path) {
        if (stats_export_csv(&g_ftl.stats, csv_path) != 0) {
            fprintf(stderr, "Failed to export CSV: %s\n", csv_path);
            rc = 1;
        }
    }

out:
    ftl_destroy();
    nvme_controller_destroy(&nvme);
    request_queue_destroy(&request_queue);
    return rc;
}
