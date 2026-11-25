#ifndef PROZEST_PMM_CONTIGUOUS_H
#define PROZEST_PMM_CONTIGUOUS_H
#include "pmm_safety.h"
/* First-fit within a single managed range. Power-of-two physical page
 * alignment. Caller owns every page freed; no allocation extent ownership
 * tracking. Trusted writable output, immutable plan, BSP-only, IF=0. */
enum pmm_status pmm_bitmap_alloc_contiguous(struct pmm_bitmap_state *s,
    uint64_t count, uint64_t alignment_pages, uint64_t *physical_out);
enum pmm_status pmm_bitmap_free_contiguous(struct pmm_bitmap_state *s,
    uint64_t physical, uint64_t count);
#endif
