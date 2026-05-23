#include "stats.h"

#include <inttypes.h>

void stats_init(ssd_statistics_t *stats)
{
    memset(stats, 0, sizeof(*stats));
}

double stats_write_amplification(const ssd_statistics_t *stats)
{
    if (stats->host_page_count == 0) {
        return 0.0;
    }
    return (double)stats->nand_write_count / (double)stats->host_page_count;
}

uint64_t stats_average_latency_us(const ssd_statistics_t *stats)
{
    if (stats->host_request_count == 0) {
        return 0;
    }
    return stats->total_latency_us / stats->host_request_count;
}

void stats_update_request(ssd_statistics_t *stats,
                          uint64_t queue_latency_us,
                          uint64_t service_latency_us,
                          uint64_t total_latency_us)
{
    stats->total_queue_latency_us += queue_latency_us;
    stats->total_service_latency_us += service_latency_us;
    stats->total_latency_us += total_latency_us;

    if (queue_latency_us > stats->max_queue_latency_us) {
        stats->max_queue_latency_us = queue_latency_us;
    }
    if (service_latency_us > stats->max_service_latency_us) {
        stats->max_service_latency_us = service_latency_us;
    }
    if (total_latency_us > stats->max_latency_us) {
        stats->max_latency_us = total_latency_us;
    }
}

void stats_print(const ssd_statistics_t *stats)
{
    double wa = stats_write_amplification(stats);

    printf("\n=== SSD Statistics ===\n");
    printf("Host Requests          : %" PRIu64 "\n", stats->host_request_count);
    printf("Host Pages             : %" PRIu64 "\n", stats->host_page_count);
    printf("NAND Writes            : %" PRIu64 "\n", stats->nand_write_count);
    printf("NAND Reads             : %" PRIu64 "\n", stats->nand_read_count);
    printf("NAND Erases            : %" PRIu64 "\n", stats->nand_erase_count);
    printf("GC Count               : %" PRIu64 "\n", stats->gc_count);
    printf("Migrated Pages         : %" PRIu64 "\n", stats->migrated_page_count);
    printf("Foreground GC Count    : %" PRIu64 "\n", stats->foreground_gc_count);
    printf("Background GC Count    : %" PRIu64 "\n", stats->background_gc_count);
    printf("Sequential Writes      : %" PRIu64 "\n", stats->sequential_write_count);
    printf("Random Writes          : %" PRIu64 "\n", stats->random_write_count);
    printf("Write Amplification    : %.2f\n", wa);
    printf("Avg Queue Latency(us)  : %" PRIu64 "\n", stats->host_request_count ? (stats->total_queue_latency_us / stats->host_request_count) : 0);
    printf("Avg Service Latency(us): %" PRIu64 "\n", stats->host_request_count ? (stats->total_service_latency_us / stats->host_request_count) : 0);
    printf("Avg Latency(us)        : %" PRIu64 "\n", stats_average_latency_us(stats));
    printf("Max Queue Latency(us)  : %" PRIu64 "\n", stats->max_queue_latency_us);
    printf("Max Service Latency(us): %" PRIu64 "\n", stats->max_service_latency_us);
    printf("Max Latency(us)        : %" PRIu64 "\n", stats->max_latency_us);
}

int stats_export_csv(const ssd_statistics_t *stats, const char *path)
{
    FILE *fp = fopen(path, "w");
    if (!fp) {
        return -1;
    }

    fprintf(fp, "host_requests,host_pages,nand_writes,nand_reads,nand_erases,gc_count,migrated_pages,wa,avg_latency_us,max_latency_us\n");
    fprintf(fp, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.4f,%" PRIu64 ",%" PRIu64 "\n",
            stats->host_request_count,
            stats->host_page_count,
            stats->nand_write_count,
            stats->nand_read_count,
            stats->nand_erase_count,
            stats->gc_count,
            stats->migrated_page_count,
            stats_write_amplification(stats),
            stats_average_latency_us(stats),
            stats->max_latency_us);

    fclose(fp);
    return 0;
}
