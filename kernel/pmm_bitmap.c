#include "pmm_bitmap.h"
#define PAGE UINT64_C(4096)

/* Check the source bounds before calling the M10.3 validator. */
int pmm_bitmap_plan_check(const struct pmm_plan *p,
                          const struct reservation_inventory *s,
                          unsigned bits)
{
    if (!p || !s || bits < 32 || bits > 52 || !s->eligible_count ||
        s->eligible_count > ELIGIBLE_CAPACITY ||
        s->merged_count > RESERVATION_CAPACITY || !p->managed_pages)
        return 0;
    uint64_t total = 0, maximum = UINT64_C(1) << bits;
    for (uint32_t i = 0; i < s->merged_count; ++i) {
        const struct physical_range *r = &s->merged[i];
        if (r->base >= r->end || r->end > maximum ||
            ((r->base | r->end) & (PAGE - 1))) return 0;
    }
    for (uint32_t i = 0; i < s->eligible_count; ++i) {
        const struct physical_range *r = &s->eligible[i];
        if (!r->base || r->base >= r->end || r->end > maximum ||
            ((r->base | r->end) & (PAGE - 1)) ||
            (i && s->eligible[i-1].end > r->base)) return 0;
        uint64_t n = (r->end - r->base) / PAGE;
        if (total > UINT64_MAX - n) return 0;
        total += n;
        for (uint32_t j = 0; j < s->merged_count; ++j)
            if (r->base < s->merged[j].end && s->merged[j].base < r->end)
                return 0;
    }
    uint64_t required;
    if (total != s->eligible_pages || p->premetadata_pages != total ||
        !pmm_bitmap_bytes(total, &required) ||
        p->bitmap_required_bytes != required ||
        p->bitmap_storage_pages != required / PAGE + (required % PAGE != 0) ||
        p->bitmap_storage_pages > SIZE_MAX / PAGE ||
        p->managed_pages > total || p->bitmap_storage_pages >= total ||
        p->managed_pages != total - p->bitmap_storage_pages)
        return 0;
    return pmm_plan_validate(p, s, bits);
}

/* LSB-first: 0 = free, 1 = allocated or unavailable. */
static uint8_t initial_byte(uint64_t managed, size_t i)
{
    uint64_t full = managed / 8;
    unsigned remainder = (unsigned)(managed % 8);
    if (i < full) return 0;
    if (i == full && remainder) return (uint8_t)(0xffu << remainder);
    return UINT8_C(0xff);
}

static int disjoint(const volatile void *a, size_t an,
                    const volatile void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (an > UINTPTR_MAX - x || bn > UINTPTR_MAX - y) return 0;
    return x + an <= y || y + bn <= x;
}

int pmm_bitmap_initial_check(const struct pmm_bitmap_state *s)
{
    if (!s || !s->initialized || !s->plan || !s->bitmap ||
        !s->plan->managed_pages || s->plan->bitmap_storage_pages > SIZE_MAX / PAGE ||
        s->storage_bytes != s->plan->bitmap_storage_pages * PAGE ||
        s->stats.managed_pages != s->plan->managed_pages ||
        s->stats.free_pages != s->stats.managed_pages ||
        s->stats.allocated_pages || s->search_hint ||
        s->stats.managed_pages > s->plan->bitmap_capacity_bits) return 0;
    for (size_t i = 0; i < s->storage_bytes; ++i)
        if (s->bitmap[i] != initial_byte(s->stats.managed_pages, i)) return 0;
    return 1;
}

enum pmm_status pmm_bitmap_state_init(struct pmm_bitmap_state *state,
    const struct pmm_plan *plan, const struct reservation_inventory *source,
    unsigned bits, volatile uint8_t *buffer, size_t bytes)
{
    if (!state) return PMM_INVALID_ADDRESS;
    if (state->initialized) return PMM_ALREADY_INITIALIZED;
    if (!buffer || !pmm_bitmap_plan_check(plan, source, bits) ||
        bytes != plan->bitmap_storage_pages * PAGE ||
        !disjoint(buffer, bytes, state, sizeof(*state)) ||
        !disjoint(buffer, bytes, plan, sizeof(*plan)) ||
        !disjoint(buffer, bytes, source, sizeof(*source)) ||
        !disjoint(state, sizeof(*state), plan, sizeof(*plan)) ||
        !disjoint(state, sizeof(*state), source, sizeof(*source)))
        return PMM_INVALID_ADDRESS;
    /* Only the reserved metadata backing is written. */
    for (size_t i = 0; i < bytes; ++i)
        buffer[i] = initial_byte(plan->managed_pages, i);
    struct pmm_bitmap_state next = {
        .plan = plan, .bitmap = buffer, .storage_bytes = bytes,
        .stats = {plan->managed_pages, plan->managed_pages, 0},
        .search_hint = 0, .initialized = 1
    };
    if (!pmm_bitmap_initial_check(&next)) return PMM_UNSUPPORTED;
    *state = next;
    return PMM_OK;
}
