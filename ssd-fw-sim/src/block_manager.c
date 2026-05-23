#include "block_manager.h"

int free_block_pool_init(free_block_pool_t *pool, uint32_t capacity)
{
    if (capacity == 0) {
        return -1;
    }

    pool->items = calloc(capacity, sizeof(uint32_t));
    if (!pool->items) {
        return -1;
    }

    pool->head = 0;
    pool->tail = 0;
    pool->count = 0;
    pool->capacity = capacity;
    return 0;
}

void free_block_pool_destroy(free_block_pool_t *pool)
{
    free(pool->items);
    pool->items = NULL;
    pool->head = pool->tail = pool->count = pool->capacity = 0;
}

bool free_block_pool_push(free_block_pool_t *pool, uint32_t block_index)
{
    if (pool->count == pool->capacity) {
        return false;
    }

    pool->items[pool->tail] = block_index;
    pool->tail = (pool->tail + 1U) % pool->capacity;
    pool->count++;
    return true;
}

bool free_block_pool_pop(free_block_pool_t *pool, uint32_t *block_index)
{
    if (pool->count == 0) {
        return false;
    }

    *block_index = pool->items[pool->head];
    pool->head = (pool->head + 1U) % pool->capacity;
    pool->count--;
    return true;
}

bool free_block_pool_get_min_erase_block(const free_block_pool_t *pool,
                                         const uint32_t *erase_count,
                                         uint32_t *block_index)
{
    if (pool->count == 0) {
        return false;
    }

    uint32_t idx = pool->head;
    uint32_t best = pool->items[idx];
    uint32_t best_erase = erase_count[best];

    for (uint32_t i = 0; i < pool->count; i++) {
        uint32_t candidate = pool->items[idx];
        if (erase_count[candidate] < best_erase) {
            best = candidate;
            best_erase = erase_count[candidate];
        }
        idx = (idx + 1U) % pool->capacity;
    }

    *block_index = best;
    return true;
}

uint32_t free_block_pool_count(const free_block_pool_t *pool)
{
    return pool->count;
}
