#include "config.h"

#include <ctype.h>
#include <inttypes.h>

ssd_config_t g_config;
log_level_t g_log_level = LOG_LEVEL_INFO;

static bool parse_u32(const char *value, uint32_t *parsed_out)
{
    char *end = NULL;
    unsigned long parsed;

    if (value[0] == '\0') {
        return false;
    }

    parsed = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }

    *parsed_out = (uint32_t)parsed;
    return true;
}

void ssd_config_init_default(ssd_config_t *config)
{
    config->total_blocks = 128;
    config->pages_per_block = 64;
    config->logical_pages = 4096;
    config->request_queue_depth = 256;
    config->gc_free_block_threshold = 8;
    config->read_latency_us = 50;
    config->program_latency_us = 200;
    config->erase_latency_us = 1500;
    config->trace_inter_arrival_us = 10;
}

static void trim_line(char *line)
{
    size_t len = strlen(line);

    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || isspace((unsigned char)line[len - 1]))) {
        line[--len] = '\0';
    }

    while (*line && isspace((unsigned char)*line)) {
        memmove(line, line + 1, strlen(line));
    }
}

int ssd_config_load_file(const char *path, ssd_config_t *config)
{
    FILE *fp = fopen(path, "r");
    char line[256];
    uint32_t line_number = 0;

    if (!fp) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *eq;
        char *key;
        char *value;
        uint32_t parsed = 0;

        line_number++;

        trim_line(line);

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        eq = strchr(line, '=');
        if (!eq) {
            LOG_ERROR("Malformed config line %" PRIu32, line_number);
            fclose(fp);
            return -1;
        }

        *eq = '\0';
        key = line;
        value = eq + 1;

        trim_line(key);
        trim_line(value);

        if (!parse_u32(value, &parsed)) {
            LOG_ERROR("Invalid numeric value for key '%s' on line %" PRIu32,
                      key,
                      line_number);
            fclose(fp);
            return -1;
        }

        if (strcmp(key, "total_blocks") == 0) {
            config->total_blocks = parsed;
        } else if (strcmp(key, "pages_per_block") == 0) {
            config->pages_per_block = parsed;
        } else if (strcmp(key, "logical_pages") == 0) {
            config->logical_pages = parsed;
        } else if (strcmp(key, "request_queue_depth") == 0) {
            config->request_queue_depth = parsed;
        } else if (strcmp(key, "gc_free_block_threshold") == 0) {
            config->gc_free_block_threshold = parsed;
        } else if (strcmp(key, "read_latency_us") == 0) {
            config->read_latency_us = parsed;
        } else if (strcmp(key, "program_latency_us") == 0) {
            config->program_latency_us = parsed;
        } else if (strcmp(key, "erase_latency_us") == 0) {
            config->erase_latency_us = parsed;
        } else if (strcmp(key, "trace_inter_arrival_us") == 0) {
            config->trace_inter_arrival_us = parsed;
        } else {
            LOG_ERROR("Unknown config key '%s' on line %" PRIu32,
                      key,
                      line_number);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

int ssd_config_validate(const ssd_config_t *config)
{
    uint64_t physical_pages;

    if (!config) {
        return -1;
    }

    if (config->total_blocks == 0 ||
        config->pages_per_block == 0 ||
        config->logical_pages == 0 ||
        config->request_queue_depth == 0) {
        return -1;
    }

    physical_pages = (uint64_t)config->total_blocks * (uint64_t)config->pages_per_block;
    if (physical_pages == 0 || physical_pages > UINT32_MAX) {
        return -1;
    }

    if (config->gc_free_block_threshold == 0 ||
        config->gc_free_block_threshold >= config->total_blocks) {
        LOG_ERROR("gc_free_block_threshold (%u) must be between 1 and %u",
                  config->gc_free_block_threshold,
                  config->total_blocks - 1U);
        return -1;
    }

    if (config->logical_pages > physical_pages) {
        LOG_ERROR("logical_pages (%u) cannot exceed physical capacity (%" PRIu64 ")",
                  config->logical_pages,
                  physical_pages);
        return -1;
    }

    return 0;
}

void ssd_config_print(const ssd_config_t *config)
{
    printf("=== SSD Configuration ===\n");
    printf("total_blocks           : %u\n", config->total_blocks);
    printf("pages_per_block        : %u\n", config->pages_per_block);
    printf("logical_pages          : %u\n", config->logical_pages);
    printf("request_queue_depth    : %u\n", config->request_queue_depth);
    printf("gc_free_block_threshold: %u\n", config->gc_free_block_threshold);
    printf("read_latency_us        : %u\n", config->read_latency_us);
    printf("program_latency_us     : %u\n", config->program_latency_us);
    printf("erase_latency_us       : %u\n", config->erase_latency_us);
    printf("trace_inter_arrival_us : %u\n", config->trace_inter_arrival_us);
}
