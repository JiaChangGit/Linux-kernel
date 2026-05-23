#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED(x) ((void)(x))

typedef enum {
    LOG_LEVEL_NONE = -1,
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
} log_level_t;

extern log_level_t g_log_level;

#define LOG_ENABLED(level) ((int)(level) <= (int)g_log_level)

#define LOG_ERROR(fmt, ...) \
    do { \
        if (LOG_ENABLED(LOG_LEVEL_ERROR)) { \
            fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_WARN(fmt, ...) \
    do { \
        if (LOG_ENABLED(LOG_LEVEL_WARN)) { \
            fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_INFO(fmt, ...) \
    do { \
        if (LOG_ENABLED(LOG_LEVEL_INFO)) { \
            fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_DEBUG(fmt, ...) \
    do { \
        if (LOG_ENABLED(LOG_LEVEL_DEBUG)) { \
            fprintf(stdout, "[DEBUG] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

#endif
