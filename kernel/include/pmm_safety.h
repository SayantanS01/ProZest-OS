#ifndef PROZEST_PMM_SAFETY_H
#define PROZEST_PMM_SAFETY_H
#include "pmm_free.h"
/* Trusted, writable, naturally aligned output; no alternate virtual aliases
 * of PMM-owned storage. BSP-only, IF=0; immutable validated plan. */
enum pmm_status pmm_bitmap_get_stats(const struct pmm_bitmap_state *state, struct pmm_stats *out);
#endif
