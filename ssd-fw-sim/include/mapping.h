#ifndef MAPPING_H
#define MAPPING_H

#include "nand.h"

/* 一筆 L2P mapping。valid=false 表示該 LPN 尚未被寫入。 */
typedef struct {
    bool valid;
    physical_page_address_t ppa;
} mapping_entry_t;

/* Mapping table 由 FTL 擁有；mapping.c 只負責欄位操作。 */
int mapping_table_init(mapping_entry_t *table, uint32_t entry_count);
bool mapping_get_physical_page(const mapping_entry_t *table,
                               uint32_t lpn,
                               physical_page_address_t *ppa);
void mapping_set_physical_page(mapping_entry_t *table,
                               uint32_t lpn,
                               const physical_page_address_t *ppa);
void mapping_clear(mapping_entry_t *table, uint32_t lpn);

#endif
