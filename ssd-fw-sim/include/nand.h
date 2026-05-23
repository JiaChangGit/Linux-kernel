#ifndef NAND_H
#define NAND_H

#include "block_manager.h"
#include "common.h"
#include "config.h"

typedef enum {
    NAND_PAGE_FREE = 0,
    NAND_PAGE_VALID,
    NAND_PAGE_INVALID,
} nand_page_state_t;

typedef struct {
    uint32_t block_index;
    uint32_t page_index;
} physical_page_address_t;

typedef struct {
    nand_page_state_t state;
    uint32_t logical_page_number;
    bool has_logical_page;
} nand_page_t;

typedef struct {
    nand_page_t *pages;
    uint32_t valid_page_count;
    uint32_t invalid_page_count;
    uint32_t free_page_count;
    uint32_t erase_count;
    uint32_t write_pointer;
} nand_block_t;

typedef struct {
    nand_block_t *blocks;
    uint32_t total_blocks;
    uint32_t pages_per_block;
} nand_ssd_t;


int nand_init(nand_ssd_t *nand, const ssd_config_t *config);
void nand_destroy(nand_ssd_t *nand);
bool nand_allocate_page(nand_ssd_t *nand,
                        free_block_pool_t *free_pool,
                        uint32_t *current_write_block,
                        physical_page_address_t *ppa);
void nand_program_page(nand_ssd_t *nand,
                       const physical_page_address_t *ppa,
                       uint32_t logical_page_number);
void nand_invalidate_page(nand_ssd_t *nand,
                          const physical_page_address_t *ppa);
void nand_erase_block(nand_ssd_t *nand, uint32_t block_index);
bool nand_is_block_free(const nand_ssd_t *nand, uint32_t block_index);
uint32_t nand_get_free_block_count(const nand_ssd_t *nand);

#endif
