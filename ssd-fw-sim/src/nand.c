#include "nand.h"
#include "config.h"

static void nand_reset_block(nand_block_t *block, uint32_t pages_per_block)
{
    /* Erase 後整個 block 回到可寫入狀態，write pointer 也歸零。 */
    for (uint32_t page = 0; page < pages_per_block; page++) {
        block->pages[page].state = NAND_PAGE_FREE;
        block->pages[page].logical_page_number = 0;
        block->pages[page].has_logical_page = false;
    }

    block->valid_page_count = 0;
    block->invalid_page_count = 0;
    block->free_page_count = pages_per_block;
    block->write_pointer = 0;
}

int nand_init(nand_ssd_t *nand, const ssd_config_t *config)
{
    nand->total_blocks = config->total_blocks;
    nand->pages_per_block = config->pages_per_block;
    nand->blocks = calloc(nand->total_blocks, sizeof(nand_block_t));
    if (!nand->blocks) {
        return -1;
    }

    for (uint32_t block = 0; block < nand->total_blocks; block++) {
        nand->blocks[block].pages = calloc(nand->pages_per_block, sizeof(nand_page_t));
        if (!nand->blocks[block].pages) {
            for (uint32_t i = 0; i < block; i++) {
                free(nand->blocks[i].pages);
            }
            free(nand->blocks);
            nand->blocks = NULL;
            return -1;
        }

        nand_reset_block(&nand->blocks[block], nand->pages_per_block);
    }

    return 0;
}

void nand_destroy(nand_ssd_t *nand)
{
    if (!nand->blocks) {
        return;
    }

    for (uint32_t block = 0; block < nand->total_blocks; block++) {
        free(nand->blocks[block].pages);
    }
    free(nand->blocks);
    nand->blocks = NULL;
    nand->total_blocks = 0;
    nand->pages_per_block = 0;
}

bool nand_is_block_free(const nand_ssd_t *nand, uint32_t block_index)
{
    const nand_block_t *block = &nand->blocks[block_index];
    return block->free_page_count == nand->pages_per_block;
}

uint32_t nand_get_free_block_count(const nand_ssd_t *nand)
{
    uint32_t count = 0;

    for (uint32_t block = 0; block < nand->total_blocks; block++) {
        if (nand_is_block_free(nand, block)) {
            count++;
        }
    }

    return count;
}

bool nand_allocate_page(nand_ssd_t *nand,
                        free_block_pool_t *free_pool,
                        uint32_t *current_write_block,
                        physical_page_address_t *ppa)
{
    nand_block_t *block = &nand->blocks[*current_write_block];

    /* 目前 block 寫滿後，才從 free pool 切到下一個 block。 */
    if (block->write_pointer >= nand->pages_per_block) {
        if (!free_block_pool_pop(free_pool, current_write_block)) {
            return false;
        }
        block = &nand->blocks[*current_write_block];
    }

    if (block->free_page_count == 0) {
        return false;
    }

    ppa->block_index = *current_write_block;
    ppa->page_index = block->write_pointer;
    block->write_pointer++;
    return true;
}

void nand_program_page(nand_ssd_t *nand,
                       const physical_page_address_t *ppa,
                       uint32_t logical_page_number)
{
    nand_block_t *block = &nand->blocks[ppa->block_index];
    nand_page_t *page = &block->pages[ppa->page_index];

    /* logical_page_number 模擬 OOB metadata，GC 搬移時會靠它更新 L2P。 */
    page->state = NAND_PAGE_VALID;
    page->logical_page_number = logical_page_number;
    page->has_logical_page = true;
    block->valid_page_count++;
    block->free_page_count--;
}

void nand_invalidate_page(nand_ssd_t *nand,
                          const physical_page_address_t *ppa)
{
    nand_block_t *block = &nand->blocks[ppa->block_index];
    nand_page_t *page = &block->pages[ppa->page_index];

    /* 非 VALID 頁不重複 invalid，避免 counter 被扣錯。 */
    if (page->state != NAND_PAGE_VALID) {
        return;
    }

    page->state = NAND_PAGE_INVALID;
    block->valid_page_count--;
    block->invalid_page_count++;
}

void nand_erase_block(nand_ssd_t *nand, uint32_t block_index)
{
    nand_block_t *block = &nand->blocks[block_index];

    nand_reset_block(block, nand->pages_per_block);
    block->erase_count++;
}
