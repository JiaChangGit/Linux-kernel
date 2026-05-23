#ifndef MAPPING_H
#define MAPPING_H

#include "nand.h"

typedef struct {
    bool valid;
    physical_page_address_t ppa;
} mapping_entry_t;

int mapping_table_init(mapping_entry_t *table, uint32_t entry_count);
bool mapping_get_physical_page(const mapping_entry_t *table,
                               uint32_t lpn,
                               physical_page_address_t *ppa);
void mapping_set_physical_page(mapping_entry_t *table,
                               uint32_t lpn,
                               const physical_page_address_t *ppa);
void mapping_clear(mapping_entry_t *table, uint32_t lpn);

#endif
