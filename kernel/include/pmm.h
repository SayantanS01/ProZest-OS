#ifndef PROZEST_PMM_H
#define PROZEST_PMM_H
#include <stdint.h>
/* M10.4: initialization and statistics are live. Allocation/free remain future APIs. */
enum pmm_status { PMM_OK=0, PMM_NOT_INITIALIZED, PMM_ALREADY_INITIALIZED,
    PMM_OUT_OF_MEMORY, PMM_INVALID_ADDRESS, PMM_DOUBLE_FREE,
    PMM_RESERVED, PMM_INVALID_COUNT, PMM_UNSUPPORTED };
struct pmm_stats { uint64_t managed_pages, free_pages, allocated_pages; };
struct pmm_plan;
struct reservation_inventory;
struct memory_inventory;
/* Plan and its backing storage must remain valid and immutable for PMM lifetime. */
enum pmm_status pmm_init(const struct pmm_plan *plan,
    const struct reservation_inventory *source,
    const struct memory_inventory *map, unsigned physical_bits);
int pmm_initial_state_check(void);
enum pmm_status pmm_alloc_page(uint64_t *physical_out);
enum pmm_status pmm_free_page(uint64_t physical);
enum pmm_status pmm_alloc_contiguous(uint64_t count, uint64_t alignment_pages,
                                     uint64_t *physical_out);
enum pmm_status pmm_free_contiguous(uint64_t physical, uint64_t count);
enum pmm_status pmm_get_stats(struct pmm_stats *out);
int pmm_is_managed_address(uint64_t physical);
#endif
