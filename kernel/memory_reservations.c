#include <stdint.h>
#include "memory_reservations.h"
#define PAGE UINT64_C(4096)
int reservation_add(struct reservation_inventory *r, uint64_t base, uint64_t end,
                    uint32_t reason, unsigned bits)
{
    if (!r || !reason || bits < 32 || bits > 63 || base >= end ||
        end > (UINT64_C(1) << bits) || r->item_count == RESERVATION_CAPACITY ||
        end > UINT64_MAX - (PAGE - 1)) return 0;
    struct reservation_item *item=&r->item[r->item_count++];
    item->range.base=base & ~(PAGE-1);
    item->range.end=(end+PAGE-1) & ~(PAGE-1);
    item->reason=reason;
    return 1;
}
static void sort_items(struct reservation_inventory *r)
{
    for (uint32_t i=1;i<r->item_count;i++) {
        struct reservation_item key=r->item[i];
        uint32_t j=i;
        while (j && (r->item[j-1].range.base > key.range.base ||
               (r->item[j-1].range.base == key.range.base && r->item[j-1].range.end > key.range.end))) {
            r->item[j]=r->item[j-1]; --j;
        }
        r->item[j]=key;
    }
}
static int emit(struct reservation_inventory *r, uint64_t base, uint64_t end)
{
    if (base>=end) return 1;
    if (r->eligible_count==ELIGIBLE_CAPACITY) return 0;
    uint64_t bytes=end-base;
    if (UINT64_MAX-r->eligible_bytes<bytes) return 0;
    r->eligible[r->eligible_count++]=(struct physical_range){base,end};
    r->eligible_bytes+=bytes;
    r->eligible_pages+=bytes/PAGE;
    if (bytes>r->largest_bytes) r->largest_bytes=bytes;
    return 1;
}
int reservations_build(struct reservation_inventory *r, const struct memory_inventory *map)
{
    if (!r || !map || map->count>MEMORY_MAP_CAPACITY) return 0;
    r->merged_count=r->eligible_count=0;
    r->excluded_pages=r->eligible_pages=r->eligible_bytes=r->largest_bytes=0;
    sort_items(r);
    for (uint32_t i=0;i<r->item_count;i++) {
        struct physical_range x=r->item[i].range;
        if (r->merged_count && x.base<=r->merged[r->merged_count-1].end) {
            if (x.end>r->merged[r->merged_count-1].end) r->merged[r->merged_count-1].end=x.end;
        } else r->merged[r->merged_count++]=x;
    }
    for (uint64_t i=0;i<map->count;i++) if (map->region[i].type==LIMINE_MEMMAP_USABLE) {
        uint64_t start=(map->region[i].base+PAGE-1)&~(PAGE-1);
        uint64_t end=map->region[i].end & ~(PAGE-1);
        if (start>end) start=end;
        for (uint32_t j=0;j<r->merged_count && start<end;j++) {
            const struct physical_range *p=&r->merged[j];
            if (p->end<=start) continue;
            if (p->base>=end) break;
            if (p->base>start && !emit(r,start,p->base<end?p->base:end)) return 0;
            if (p->end>start) start=p->end<end?p->end:end;
        }
        if (!emit(r,start,end)) return 0;
    }
    if (r->eligible_pages>map->candidates || r->eligible_bytes!=r->eligible_pages*PAGE) return 0;
    r->excluded_pages=map->candidates-r->eligible_pages;
    for (uint32_t i=0;i<r->eligible_count;i++) {
        struct physical_range e=r->eligible[i];
        if ((e.base|e.end)&(PAGE-1) || e.base>=e.end ||
            (i && r->eligible[i-1].end>e.base)) return 0;
        int inside=0;
        for (uint64_t j=0;j<map->count;j++) if (map->region[j].type==LIMINE_MEMMAP_USABLE &&
            map->region[j].base<=e.base && e.end<=map->region[j].end) inside=1;
        if (!inside) return 0;
        for (uint32_t j=0;j<r->merged_count;j++) if (e.base<r->merged[j].end && r->merged[j].base<e.end) return 0;
    }
    return 1;
}
