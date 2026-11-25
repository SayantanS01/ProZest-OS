#ifndef PROZEST_PMM_PLAN_H
#define PROZEST_PMM_PLAN_H
#include <stdint.h>
#include "memory_reservations.h"
#define PMM_PLAN_RANGE_CAPACITY ELIGIBLE_CAPACITY
struct pmm_managed_range { uint64_t base, end, first_index; };
struct pmm_plan {
    struct pmm_managed_range range[PMM_PLAN_RANGE_CAPACITY];
    uint32_t range_count;
    uint32_t initialized; /* Always zero in M10.3. */
    uint64_t premetadata_pages, bitmap_required_bytes, bitmap_storage_pages;
    uint64_t bitmap_capacity_bits, unused_trailing_bits;
    uint64_t metadata_base, metadata_end, metadata_total_bytes;
    uint64_t managed_pages, managed_bytes, free_pages, allocated_pages, search_hint;
};
int pmm_bitmap_bytes(uint64_t pages, uint64_t *bytes);
int pmm_plan_build(struct pmm_plan *out, const struct reservation_inventory *source,
                   unsigned physical_bits);
int pmm_plan_validate(const struct pmm_plan *plan, const struct reservation_inventory *source,
                      unsigned physical_bits);
int pmm_page_to_index(const struct pmm_plan *plan, uint64_t physical, uint64_t *index);
int pmm_index_to_page(const struct pmm_plan *plan, uint64_t index, uint64_t *physical);
int pmm_is_managed(const struct pmm_plan *plan, uint64_t physical);
#endif
