#ifndef PROZEST_PMM_FREE_H
#define PROZEST_PMM_FREE_H
#include "pmm_alloc.h"
/* Same single-CPU, IF=0, immutable-plan contract as allocation.
 * Caller must release all uses of a frame before freeing it. */
enum pmm_status pmm_bitmap_free_page(struct pmm_bitmap_state *state, uint64_t physical);
#endif
