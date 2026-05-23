#include "mapping.h"

int mapping_table_init(mapping_entry_t *table, uint32_t entry_count)
{
    memset(table, 0, sizeof(mapping_entry_t) * entry_count);
    return 0;
}

bool mapping_get_physical_page(const mapping_entry_t *table,
                               uint32_t lpn,
                               physical_page_address_t *ppa)
{
    if (!table[lpn].valid) {
        return false;
    }

    *ppa = table[lpn].ppa;
    return true;
}

void mapping_set_physical_page(mapping_entry_t *table,
                               uint32_t lpn,
                               const physical_page_address_t *ppa)
{
    table[lpn].valid = true;
    table[lpn].ppa = *ppa;
}

void mapping_clear(mapping_entry_t *table, uint32_t lpn)
{
    table[lpn].valid = false;
}
