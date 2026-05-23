#include "gc.h"

static int gc_select_victim_block(const ftl_context_t *ftl)
{
    int victim = -1;
    uint32_t max_invalid = 0;

    for (uint32_t block = 0; block < ftl->nand.total_blocks; block++) {
        const nand_block_t *candidate = &ftl->nand.blocks[block];

        if (block == ftl->current_write_block) {
            continue;
        }

        if (candidate->invalid_page_count == 0) {
            continue;
        }

        if ((victim < 0) || (candidate->invalid_page_count > max_invalid)) {
            victim = (int)block;
            max_invalid = candidate->invalid_page_count;
        }
    }

    if (victim < 0) {
        for (uint32_t block = 0; block < ftl->nand.total_blocks; block++) {
            const nand_block_t *candidate = &ftl->nand.blocks[block];
            if (block == ftl->current_write_block) {
                continue;
            }
            if (candidate->invalid_page_count > 0) {
                victim = (int)block;
                break;
            }
        }
    }

    return victim;
}

static bool gc_migrate_valid_pages(ftl_context_t *ftl, uint32_t victim_block_index)
{
    nand_block_t *victim = &ftl->nand.blocks[victim_block_index];

    for (uint32_t page_index = 0; page_index < ftl->nand.pages_per_block; page_index++) {
        nand_page_t *page = &victim->pages[page_index];

        if (page->state != NAND_PAGE_VALID) {
            continue;
        }

        physical_page_address_t old_ppa = {
            .block_index = victim_block_index,
            .page_index = page_index,
        };
        physical_page_address_t new_ppa;

        if (!nand_allocate_page(&ftl->nand,
                                &ftl->free_block_pool,
                                &ftl->current_write_block,
                                &new_ppa)) {
            return false;
        }

        ftl->current_time_us += ftl->config->read_latency_us;
        ftl->stats.nand_read_count++;

        nand_program_page(&ftl->nand, &new_ppa, page->logical_page_number);
        ftl->current_time_us += ftl->config->program_latency_us;
        ftl->stats.nand_write_count++;
        ftl->stats.migrated_page_count++;

        mapping_set_physical_page(ftl->mapping_table,
                                  page->logical_page_number,
                                  &new_ppa);

        nand_invalidate_page(&ftl->nand, &old_ppa);
    }

    return true;
}

bool gc_needed(const ftl_context_t *ftl)
{
    return free_block_pool_count(&ftl->free_block_pool) <
           ftl->config->gc_free_block_threshold;
}

bool gc_run(ftl_context_t *ftl, bool foreground)
{
    int victim = gc_select_victim_block(ftl);
    if (victim < 0) {
        return false;
    }

    if (foreground) {
        ftl->stats.foreground_gc_count++;
    } else {
        ftl->stats.background_gc_count++;
    }

    ftl->gc_stall_active = foreground;
    LOG_INFO("GC select victim block=%d invalid=%u valid=%u",
             victim,
             ftl->nand.blocks[victim].invalid_page_count,
             ftl->nand.blocks[victim].valid_page_count);

    if (!gc_migrate_valid_pages(ftl, (uint32_t)victim)) {
        ftl->gc_stall_active = false;
        LOG_ERROR("GC migration failed for block=%d", victim);
        return false;
    }

    nand_erase_block(&ftl->nand, (uint32_t)victim);
    ftl->current_time_us += ftl->config->erase_latency_us;
    ftl->stats.nand_erase_count++;
    ftl->stats.gc_count++;

    if (!free_block_pool_push(&ftl->free_block_pool, (uint32_t)victim)) {
        ftl->gc_stall_active = false;
        LOG_ERROR("Free block pool is full while returning victim block=%d", victim);
        return false;
    }

    LOG_INFO("GC erased block=%d", victim);
    ftl->gc_stall_active = false;
    return true;
}
