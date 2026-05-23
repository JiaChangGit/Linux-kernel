#ifndef BLOCK_MANAGER_H
#define BLOCK_MANAGER_H

#include "common.h"

typedef struct {
    uint32_t *items;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t capacity;
} free_block_pool_t;

int free_block_pool_init(free_block_pool_t *pool, uint32_t capacity);
void free_block_pool_destroy(free_block_pool_t *pool);
bool free_block_pool_push(free_block_pool_t *pool, uint32_t block_index);
bool free_block_pool_pop(free_block_pool_t *pool, uint32_t *block_index);
bool free_block_pool_get_min_erase_block(const free_block_pool_t *pool,
                                         const uint32_t *erase_count,
                                         uint32_t *block_index);
uint32_t free_block_pool_count(const free_block_pool_t *pool);

#endif
