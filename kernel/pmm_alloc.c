#include "pmm_alloc.h"
#define PAGE UINT64_C(4096)

#ifdef PROZEST_PMM_SAFETY
#include "pmm_safety.h"
/* Structural check, not a substitute for the original reservation inventory.
 * The plan must remain immutable. Bound the scan before inspecting ranges. */
static int ranges_ok(const struct pmm_plan *p, uint64_t n, size_t bytes)
{
    if (!p->metadata_base || p->metadata_base>=p->metadata_end ||
        ((p->metadata_base|p->metadata_end)&(PAGE-1)) ||
        p->metadata_end>(UINT64_C(1)<<52) || p->metadata_end-p->metadata_base!=bytes ||
        n>UINT64_MAX/PAGE || p->managed_bytes!=n*PAGE ||
        p->bitmap_storage_pages>UINT64_MAX-n ||
        p->premetadata_pages!=n+p->bitmap_storage_pages ||
        p->premetadata_pages>p->bitmap_capacity_bits) return 0;
    uint64_t next=0;
    for (uint32_t j=0;j<p->range_count;j++) {
        const struct pmm_managed_range *r=&p->range[j];
        if (!r->base || r->base>=r->end || ((r->base|r->end)&(PAGE-1)) ||
            r->end>(UINT64_C(1)<<52) || r->first_index!=next ||
            (j && p->range[j-1].end>r->base) ||
            (r->base<p->metadata_end && p->metadata_base<r->end)) return 0;
        uint64_t count=(r->end-r->base)/PAGE;
        if (count>n-next) return 0;
        next+=count;
    }
    return next==n;
}
#endif
static int shape_ok(const struct pmm_bitmap_state *s)
{
    if (!s || !s->initialized || !s->plan || !s->bitmap) return 0;
    const struct pmm_plan *p=s->plan;
    uint64_t n=s->stats.managed_pages;
    return n && n==p->managed_pages && p->range_count &&
        p->range_count<=PMM_PLAN_RANGE_CAPACITY &&
        p->bitmap_storage_pages<=SIZE_MAX/PAGE &&
        s->storage_bytes==p->bitmap_storage_pages*PAGE &&
        s->storage_bytes<=UINT64_MAX/8 &&
        p->bitmap_capacity_bits==(uint64_t)s->storage_bytes*8 &&
        n<=p->bitmap_capacity_bits && s->search_hint<n &&
        s->stats.free_pages<=n && s->stats.allocated_pages==n-s->stats.free_pages
#ifdef PROZEST_PMM_SAFETY
        && s->initialized==1 && ranges_ok(p,n,s->storage_bytes)
#endif
        ;
}
static int disjoint(const volatile void *a, size_t an, const volatile void *b, size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an<=UINTPTR_MAX-x && bn<=UINTPTR_MAX-y && (x+an<=y || y+bn<=x);
}
#ifdef PROZEST_PMM_SAFETY
/* Direct virtual overlap checks. Arbitrary unmapped pointers and alternate
 * physical aliases are outside this internal kernel API's contract. */
enum pmm_status pmm_bitmap_get_stats(const struct pmm_bitmap_state *s, struct pmm_stats *out)
{
    if (!s || !s->initialized) return PMM_NOT_INITIALIZED;
    if (!out || (uintptr_t)out%_Alignof(struct pmm_stats)) return PMM_INVALID_ADDRESS;
    if (!shape_ok(s)) return PMM_UNSUPPORTED;
    if (!disjoint(out,sizeof(*out),s,sizeof(*s)) ||
        !disjoint(out,sizeof(*out),s->plan,sizeof(*s->plan)) ||
        !disjoint(out,sizeof(*out),s->bitmap,s->storage_bytes)) return PMM_INVALID_ADDRESS;
    *out=s->stats;
    return PMM_OK;
}
#endif
int pmm_bitmap_live_check(const struct pmm_bitmap_state *s)
{
    if (!shape_ok(s)) return 0;
    uint64_t used=0,n=s->stats.managed_pages;
    for (uint64_t i=0;i<(uint64_t)s->storage_bytes*8;i++) {
        unsigned bit=(s->bitmap[i/8]>>(i%8))&1u;
        if (i<n) used+=bit;
        else if (!bit) return 0;
    }
    return used==s->stats.allocated_pages;
}
enum pmm_status pmm_bitmap_alloc_page(struct pmm_bitmap_state *s, uint64_t *out)
{
    if (!s || !s->initialized) return PMM_NOT_INITIALIZED;
    if (!out || (uintptr_t)out%_Alignof(uint64_t)) return PMM_INVALID_ADDRESS;
    if (!shape_ok(s)) return PMM_UNSUPPORTED;
    if (!disjoint(out,sizeof(*out),s,sizeof(*s)) ||
        !disjoint(out,sizeof(*out),s->plan,sizeof(*s->plan)) ||
        !disjoint(out,sizeof(*out),s->bitmap,s->storage_bytes)) return PMM_INVALID_ADDRESS;
    if (!s->stats.free_pages) return PMM_OUT_OF_MEMORY;
    uint64_t n=s->stats.managed_pages,i=s->search_hint;
    for (uint64_t scanned=0;scanned<n;scanned++) {
        uint8_t mask=(uint8_t)(1u<<(i%8));
        if (!(s->bitmap[i/8]&mask)) {
            uint64_t pa=0,roundtrip=0;
            if (!pmm_index_to_page(s->plan,i,&pa) || !pa || (pa&(PAGE-1)) ||
                (pa>=s->plan->metadata_base && pa<s->plan->metadata_end) ||
                !pmm_page_to_index(s->plan,pa,&roundtrip) || roundtrip!=i)
                return PMM_UNSUPPORTED;
            /* No payload access, mapping changes, or page zeroing. */
            s->bitmap[i/8]|=mask;
            --s->stats.free_pages;
            ++s->stats.allocated_pages;
            s->search_hint=(i+1==n)?0:i+1;
            *out=pa;
            return PMM_OK;
        }
        i=(i+1==n)?0:i+1;
    }
    return PMM_UNSUPPORTED; /* Counter/bitmap disagreement. */
}

#ifdef PROZEST_PMM_FREE
#include "pmm_free.h"
enum pmm_status pmm_bitmap_free_page(struct pmm_bitmap_state *s, uint64_t pa)
{
    if (!s || !s->initialized) return PMM_NOT_INITIALIZED;
    if (pa & (PAGE-1)) return PMM_INVALID_ADDRESS;
    if (!shape_ok(s)) return PMM_UNSUPPORTED;
    if (!pa || (pa>=s->plan->metadata_base && pa<s->plan->metadata_end))
        return PMM_RESERVED;
    uint64_t i=0, roundtrip=0;
    if (!pmm_page_to_index(s->plan,pa,&i)) return PMM_RESERVED;
    if (i>=s->stats.managed_pages || !pmm_index_to_page(s->plan,i,&roundtrip) || roundtrip!=pa)
        return PMM_UNSUPPORTED;
    uint8_t mask=(uint8_t)(1u<<(i%8));
    if (!(s->bitmap[i/8]&mask)) return PMM_DOUBLE_FREE;
    if (!s->stats.allocated_pages || s->stats.free_pages>=s->stats.managed_pages)
        return PMM_UNSUPPORTED;
    /* Commit only after all checks. Do not touch the frame payload. */
    s->bitmap[i/8]&=(uint8_t)~mask;
    ++s->stats.free_pages;
    --s->stats.allocated_pages;
    s->search_hint=i; /* Prefer immediate reuse of the most recently freed frame. */
    return PMM_OK;
}
#endif

#ifdef PROZEST_PMM_CONTIGUOUS
#include "pmm_contiguous.h"
#if !defined(PROZEST_PMM_SAFETY) || !defined(PROZEST_PMM_FREE)
#error Contiguous PMM requires safety and free support
#endif
/* Lowest physical first-fit, restricted to one managed range. Alignment is
 * expressed in physical pages, not dense bitmap indices. No payload access. */
enum pmm_status pmm_bitmap_alloc_contiguous(struct pmm_bitmap_state *s,
    uint64_t count, uint64_t alignment, uint64_t *out)
{
    if (!s || !s->initialized) return PMM_NOT_INITIALIZED;
    if (!count || count>UINT64_MAX/PAGE || !alignment ||
        (alignment&(alignment-1)) || alignment>UINT64_MAX/PAGE) return PMM_INVALID_COUNT;
    if (!out || (uintptr_t)out%_Alignof(uint64_t)) return PMM_INVALID_ADDRESS;
    if (!shape_ok(s)) return PMM_UNSUPPORTED;
    if (!disjoint(out,sizeof(*out),s,sizeof(*s)) ||
        !disjoint(out,sizeof(*out),s->plan,sizeof(*s->plan)) ||
        !disjoint(out,sizeof(*out),s->bitmap,s->storage_bytes)) return PMM_INVALID_ADDRESS;
    if (count>s->stats.free_pages) return PMM_OUT_OF_MEMORY;
    for (uint32_t j=0;j<s->plan->range_count;j++) {
        const struct pmm_managed_range *r=&s->plan->range[j];
        uint64_t start=r->base/PAGE, end=r->end/PAGE;
        uint64_t p=start;
        while (p<end && count<=end-p) {
            uint64_t pad=(0-p)&(alignment-1);
            if (pad>end-p || count>end-p-pad) break;
            p+=pad;
            uint64_t i=r->first_index+p-start, k=0;
            while (k<count && !(s->bitmap[(i+k)/8]&(1u<<((i+k)%8)))) ++k;
            if (k==count) {
                uint64_t pa=p*PAGE, a=0,b=0;
                if (!pmm_page_to_index(s->plan,pa,&a) || a!=i ||
                    !pmm_index_to_page(s->plan,i+count-1,&b) || b!=(p+count-1)*PAGE)
                    return PMM_UNSUPPORTED;
                for (k=0;k<count;k++) s->bitmap[(i+k)/8]|=(uint8_t)(1u<<((i+k)%8));
                s->stats.free_pages-=count;
                s->stats.allocated_pages+=count;
                s->search_hint=(i+count==s->stats.managed_pages)?0:i+count;
                *out=pa;
                return PMM_OK;
            }
            /* Skip the occupied page: no earlier start can fit this run. */
            p+=k+1;
        }
    }
    return PMM_OUT_OF_MEMORY; /* May be fragmentation, not total exhaustion. */
}

enum pmm_status pmm_bitmap_free_contiguous(struct pmm_bitmap_state *s,
    uint64_t pa, uint64_t count)
{
    if (!s || !s->initialized) return PMM_NOT_INITIALIZED;
    if (!count || count>UINT64_MAX/PAGE) return PMM_INVALID_COUNT;
    if ((pa&(PAGE-1)) || count*PAGE>UINT64_MAX-pa) return PMM_INVALID_ADDRESS;
    if (!shape_ok(s)) return PMM_UNSUPPORTED;
    if (!pa) return PMM_RESERVED;
    for (uint32_t j=0;j<s->plan->range_count;j++) {
        const struct pmm_managed_range *r=&s->plan->range[j];
        if (pa<r->base || pa>=r->end) continue;
        if (count>(r->end-pa)/PAGE) return PMM_RESERVED;
        uint64_t i=r->first_index+(pa-r->base)/PAGE;
        /* Validate the entire extent before touching a single bit. */
        for (uint64_t k=0;k<count;k++)
            if (!(s->bitmap[(i+k)/8]&(1u<<((i+k)%8)))) return PMM_DOUBLE_FREE;
        if (count>s->stats.allocated_pages) return PMM_UNSUPPORTED;
        for (uint64_t k=0;k<count;k++) s->bitmap[(i+k)/8]&=(uint8_t)~(1u<<((i+k)%8));
        s->stats.free_pages+=count;
        s->stats.allocated_pages-=count;
        s->search_hint=i;
        return PMM_OK;
    }
    return PMM_RESERVED;
}
#endif
