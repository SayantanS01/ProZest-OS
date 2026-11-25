#include "pmm_bitmap.h"
#include "pmm_mapping.h"

#ifdef PROZEST_PMM_SAFETY
#include "pmm_safety.h"
#endif
static struct pmm_bitmap_state live;

enum pmm_status pmm_init(const struct pmm_plan *plan,
    const struct reservation_inventory *source,
    const struct memory_inventory *map, unsigned bits)
{
    if (live.initialized) return PMM_ALREADY_INITIALIZED;
    if (!map || !map->count || map->count > MEMORY_MAP_CAPACITY ||
        !pmm_bitmap_plan_check(plan, source, bits)) return PMM_INVALID_ADDRESS;
    uint64_t cr3, cr4, cr0, flags;
    uint16_t cs;
    __asm__ volatile ("mov %%cr3,%0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4,%0" : "=r"(cr4));
    __asm__ volatile ("mov %%cr0,%0" : "=r"(cr0));
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags) :: "memory");
    __asm__ volatile ("mov %%cs,%0" : "=r"(cs));
    /* BSP-only, CPL0, IF=0, four-level paging. */
    if ((cs & 3) || (flags & (UINT64_C(1) << 9)) ||
        !(cr0 & (UINT64_C(1) << 31)) || !(cr0 & 1) ||
        !(cr4 & (UINT64_C(1) << 5)) || (cr4 & (UINT64_C(1) << 12)))
        return PMM_UNSUPPORTED;
    struct hhdm_table_context ctx = {map, map->hhdm};
    uint64_t verified = 0;
    if (!pmm_metadata_mapping_check(plan, map->hhdm, cr3, bits,
            page_table_hhdm_resolve, &ctx, &verified) ||
        verified != plan->bitmap_storage_pages ||
        plan->metadata_base > UINT64_MAX - map->hhdm)
        return PMM_INVALID_ADDRESS;
    volatile uint8_t *bitmap = (volatile uint8_t *)(uintptr_t)
        (map->hhdm + plan->metadata_base);
    return pmm_bitmap_state_init(&live, plan, source, bits, bitmap,
                                (size_t)(plan->bitmap_storage_pages * 4096));
}

enum pmm_status pmm_get_stats(struct pmm_stats *out)
{
#ifdef PROZEST_PMM_SAFETY
    return pmm_bitmap_get_stats(&live,out);
#else
    if (!live.initialized) return PMM_NOT_INITIALIZED;
    if (!out) return PMM_INVALID_ADDRESS;
    *out = live.stats;
    return PMM_OK;
#endif
}

int pmm_is_managed_address(uint64_t physical)
{
    return live.initialized && pmm_is_managed(live.plan, physical);
}

int pmm_initial_state_check(void)
{
    return pmm_bitmap_initial_check(&live);
}

#ifdef PROZEST_PMM_ALLOC
#include "pmm_alloc.h"
enum pmm_status pmm_alloc_page(uint64_t *physical_out)
{
    if (!live.initialized) return PMM_NOT_INITIALIZED;
    uint64_t flags;
    uint16_t cs;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags) :: "memory");
    __asm__ volatile ("mov %%cs,%0" : "=r"(cs));
    if ((cs&3) || (flags&(UINT64_C(1)<<9))) return PMM_UNSUPPORTED;
    return pmm_bitmap_alloc_page(&live,physical_out);
}
int pmm_live_state_check(void)
{
    return pmm_bitmap_live_check(&live);
}
#endif

#ifdef PROZEST_PMM_FREE
#include "pmm_free.h"
enum pmm_status pmm_free_page(uint64_t physical)
{
    if (!live.initialized) return PMM_NOT_INITIALIZED;
    uint64_t flags;
    uint16_t cs;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags) :: "memory");
    __asm__ volatile ("mov %%cs,%0" : "=r"(cs));
    if ((cs&3) || (flags&(UINT64_C(1)<<9))) return PMM_UNSUPPORTED;
    return pmm_bitmap_free_page(&live,physical);
}
#endif

#ifdef PROZEST_PMM_CONTIGUOUS
#include "pmm_contiguous.h"
static int contiguous_context_ok(void)
{
    uint64_t flags;
    uint16_t cs;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags) :: "memory");
    __asm__ volatile ("mov %%cs,%0" : "=r"(cs));
    return !(cs&3) && !(flags&(UINT64_C(1)<<9));
}
enum pmm_status pmm_alloc_contiguous(uint64_t count, uint64_t alignment_pages, uint64_t *out)
{
    if (!live.initialized) return PMM_NOT_INITIALIZED;
    if (!contiguous_context_ok()) return PMM_UNSUPPORTED;
    return pmm_bitmap_alloc_contiguous(&live,count,alignment_pages,out);
}
enum pmm_status pmm_free_contiguous(uint64_t pa, uint64_t count)
{
    if (!live.initialized) return PMM_NOT_INITIALIZED;
    if (!contiguous_context_ok()) return PMM_UNSUPPORTED;
    return pmm_bitmap_free_contiguous(&live,pa,count);
}
#endif
