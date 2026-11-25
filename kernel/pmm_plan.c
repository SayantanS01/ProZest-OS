#include <stdint.h>
#include "pmm_plan.h"
#define PAGE UINT64_C(4096)
int pmm_bitmap_bytes(uint64_t pages, uint64_t *bytes)
{
    if (!bytes) return 0;
    *bytes=pages/8+(pages%8!=0);
    return 1;
}
int pmm_page_to_index(const struct pmm_plan *plan, uint64_t physical, uint64_t *index)
{
    if (!plan || !index || (physical&(PAGE-1)) ||
        plan->range_count>PMM_PLAN_RANGE_CAPACITY) return 0;
    for (uint32_t i=0;i<plan->range_count;i++) {
        const struct pmm_managed_range *r=&plan->range[i];
        if (r->base>=r->end) return 0;
        if (physical>=r->base && physical<r->end) {
            uint64_t delta=(physical-r->base)/PAGE;
            if (r->first_index>UINT64_MAX-delta) return 0;
            *index=r->first_index+delta;
            return *index<plan->managed_pages;
        }
    }
    return 0;
}
int pmm_index_to_page(const struct pmm_plan *plan, uint64_t index, uint64_t *physical)
{
    if (!plan || !physical || index>=plan->managed_pages ||
        plan->range_count>PMM_PLAN_RANGE_CAPACITY) return 0;
    for (uint32_t i=0;i<plan->range_count;i++) {
        const struct pmm_managed_range *r=&plan->range[i];
        if (r->base>=r->end) return 0;
        uint64_t count=(r->end-r->base)/PAGE;
        if (index>=r->first_index && index-r->first_index<count) {
            uint64_t delta=index-r->first_index;
            if (delta>UINT64_MAX/PAGE || r->base>UINT64_MAX-delta*PAGE) return 0;
            *physical=r->base+delta*PAGE;
            return 1;
        }
    }
    return 0;
}
int pmm_is_managed(const struct pmm_plan *plan, uint64_t physical)
{
    uint64_t index;
    return pmm_page_to_index(plan,physical,&index);
}
int pmm_plan_validate(const struct pmm_plan *plan, const struct reservation_inventory *source,
                      unsigned bits)
{
    if (!plan || !source || bits<32 || bits>52 || !plan->metadata_base ||
        (plan->metadata_base|plan->metadata_end)&(PAGE-1) ||
        plan->metadata_base>=plan->metadata_end ||
        plan->metadata_end>(UINT64_C(1)<<bits) ||
        plan->range_count>PMM_PLAN_RANGE_CAPACITY || !plan->range_count ||
        plan->premetadata_pages!=source->eligible_pages ||
        plan->bitmap_storage_pages!=(plan->metadata_end-plan->metadata_base)/PAGE ||
        plan->bitmap_storage_pages>UINT64_MAX/(PAGE*8) ||
        plan->bitmap_storage_pages*PAGE>UINT64_MAX-sizeof(*plan) ||
        plan->bitmap_capacity_bits!=plan->bitmap_storage_pages*PAGE*8 ||
        plan->bitmap_capacity_bits<plan->premetadata_pages ||
        plan->bitmap_required_bytes>plan->bitmap_storage_pages*PAGE ||
        plan->managed_pages+plan->bitmap_storage_pages!=plan->premetadata_pages ||
        plan->managed_bytes!=plan->managed_pages*PAGE ||
        plan->free_pages!=plan->managed_pages || plan->allocated_pages ||
        plan->search_hint || plan->initialized ||
        plan->metadata_total_bytes!=sizeof(*plan)+plan->bitmap_storage_pages*PAGE ||
        plan->unused_trailing_bits!=plan->bitmap_capacity_bits-plan->premetadata_pages)
        return 0;
    int metadata_inside=0;
    for (uint32_t i=0;i<source->eligible_count;i++) {
        const struct physical_range *r=&source->eligible[i];
        if (r->base<=plan->metadata_base && plan->metadata_end<=r->end) metadata_inside=1;
    }
    if (!metadata_inside) return 0;
    uint64_t next=0;
    for (uint32_t i=0;i<plan->range_count;i++) {
        const struct pmm_managed_range *r=&plan->range[i];
        if (r->base>=r->end || ((r->base|r->end)&(PAGE-1)) ||
            r->end>(UINT64_C(1)<<bits) || r->first_index!=next ||
            (i && plan->range[i-1].end>r->base) ||
            (r->base<plan->metadata_end && plan->metadata_base<r->end)) return 0;
        int inside=0;
        for (uint32_t j=0;j<source->eligible_count;j++)
            if (source->eligible[j].base<=r->base && r->end<=source->eligible[j].end) inside=1;
        if (!inside) return 0;
        uint64_t count=(r->end-r->base)/PAGE;
        if (next>UINT64_MAX-count) return 0;
        next+=count;
    }
    return next==plan->managed_pages;
}
int pmm_plan_build(struct pmm_plan *out, const struct reservation_inventory *source,
                   unsigned bits)
{
    if (!out || !source || bits<32 || bits>52 ||
        !source->eligible_count || source->eligible_count>PMM_PLAN_RANGE_CAPACITY ||
        !source->eligible_pages) return 0;
    for (uint32_t i=0;i<source->eligible_count;i++) {
        const struct physical_range *r=&source->eligible[i];
        if (r->base>=r->end || ((r->base|r->end)&(PAGE-1)) ||
            r->end>(UINT64_C(1)<<bits) || (i && source->eligible[i-1].end>r->base)) return 0;
    }
    struct pmm_plan p;
    for (uint64_t i=0;i<sizeof(p);i++) ((unsigned char *)&p)[i]=0;
    p.premetadata_pages=source->eligible_pages;
    if (!pmm_bitmap_bytes(p.premetadata_pages,&p.bitmap_required_bytes)) return 0;
    p.bitmap_storage_pages=p.bitmap_required_bytes/PAGE+
                           (p.bitmap_required_bytes%PAGE!=0);
    if (!p.bitmap_storage_pages || p.bitmap_storage_pages>UINT64_MAX/PAGE ||
        p.bitmap_storage_pages>UINT64_MAX/(PAGE*8) ||
        p.bitmap_storage_pages>=p.premetadata_pages) return 0;
    uint64_t storage=p.bitmap_storage_pages*PAGE;
    p.bitmap_capacity_bits=storage*8;
    p.unused_trailing_bits=p.bitmap_capacity_bits-p.premetadata_pages;
    if (storage>UINT64_MAX-sizeof(p)) return 0;
    p.metadata_total_bytes=storage+sizeof(p);
    uint32_t placement=source->eligible_count;
    for (uint32_t i=0;i<source->eligible_count;i++)
        if (source->eligible[i].end-source->eligible[i].base>=storage) {placement=i;break;}
    if (placement==source->eligible_count) return 0;
    p.metadata_base=source->eligible[placement].base;
    p.metadata_end=p.metadata_base+storage;
    uint64_t next=0;
    for (uint32_t i=0;i<source->eligible_count;i++) {
        uint64_t start=source->eligible[i].base;
        if (i==placement) start=p.metadata_end;
        if (start==source->eligible[i].end) continue;
        if (p.range_count==PMM_PLAN_RANGE_CAPACITY) return 0;
        struct pmm_managed_range *r=&p.range[p.range_count++];
        *r=(struct pmm_managed_range){start,source->eligible[i].end,next};
        uint64_t count=(r->end-r->base)/PAGE;
        if (next>UINT64_MAX-count) return 0;
        next+=count;
    }
    p.managed_pages=next;
    if (next>UINT64_MAX/PAGE) return 0;
    p.managed_bytes=next*PAGE;
    p.free_pages=next; /* Planned initial counter; allocator is not initialized. */
    if (!pmm_plan_validate(&p,source,bits)) return 0;
    for (uint64_t i=0;i<sizeof(p);i++) ((unsigned char *)out)[i]=((const unsigned char *)&p)[i];
    return 1;
}
