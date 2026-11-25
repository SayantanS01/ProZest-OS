#ifndef PROZEST_PMM_MAPPING_H
#define PROZEST_PMM_MAPPING_H
#include <stdint.h>
#include "pmm_plan.h"
#include "page_table_inventory.h"
int pmm_metadata_mapping_check(const struct pmm_plan *plan, uint64_t hhdm,
                               uint64_t cr3, unsigned physical_bits,
                               page_table_resolver resolver, void *context,
                               uint64_t *verified_pages);
#endif
