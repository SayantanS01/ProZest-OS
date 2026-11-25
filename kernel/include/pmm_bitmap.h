#ifndef PROZEST_PMM_BITMAP_H
#define PROZEST_PMM_BITMAP_H
#include <stddef.h>
#include <stdint.h>
#include "pmm.h"
#include "pmm_plan.h"

/* The plan is immutable and must outlive this state. The backing buffer
 * is exclusively owned by the PMM, not part of any managed range. */
struct pmm_bitmap_state {
    const struct pmm_plan *plan;
    volatile uint8_t *bitmap;
    size_t storage_bytes;
    struct pmm_stats stats;
    uint64_t search_hint;
    unsigned initialized;
};
int pmm_bitmap_plan_check(const struct pmm_plan *plan,
                          const struct reservation_inventory *source,
                          unsigned bits);
enum pmm_status pmm_bitmap_state_init(struct pmm_bitmap_state *state,
    const struct pmm_plan *plan, const struct reservation_inventory *source,
    unsigned bits, volatile uint8_t *buffer, size_t bytes);
int pmm_bitmap_initial_check(const struct pmm_bitmap_state *state);
#endif
