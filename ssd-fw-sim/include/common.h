#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 未使用的參數可用這個巨集標記，避免編譯器警告。 */
#define UNUSED(x) ((void)(x))

/* 全域 log 等級；數字越大，會輸出越詳細的訊息。 */
typedef enum {
    LOG_LEVEL_NONE = -1,
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
} log_level_t;

extern log_level_t g_log_level;

/* 統一判斷目前 log level 是否允許輸出。 */
#define LOG_ENABLED(level) ((int)(level) <= (int)g_log_level)

/* Log 巨集集中放在 common.h，讓各模組不用重複處理格式。 */
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
