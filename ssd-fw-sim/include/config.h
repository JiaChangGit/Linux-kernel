#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

typedef struct ssd_config {
    uint32_t total_blocks;
    uint32_t pages_per_block;
    uint32_t logical_pages;
    uint32_t request_queue_depth;
    uint32_t gc_free_block_threshold;
    uint32_t read_latency_us;
    uint32_t program_latency_us;
    uint32_t erase_latency_us;
    uint32_t trace_inter_arrival_us;
} ssd_config_t;

extern ssd_config_t g_config;

void ssd_config_init_default(ssd_config_t *config);
int ssd_config_load_file(const char *path, ssd_config_t *config);
int ssd_config_validate(const ssd_config_t *config);
void ssd_config_print(const ssd_config_t *config);

#endif
