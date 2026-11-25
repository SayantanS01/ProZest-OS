#ifndef PROZEST_PMM_ALLOC_H
#define PROZEST_PMM_ALLOC_H
#include "pmm_bitmap.h"
/* BSP-only, IF=0, immutable validated plan, trusted writable output. */
enum pmm_status pmm_bitmap_alloc_page(struct pmm_bitmap_state *state, uint64_t *physical_out);
int pmm_bitmap_live_check(const struct pmm_bitmap_state *state);
int pmm_live_state_check(void);
#endif
