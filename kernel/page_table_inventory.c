#include <stdint.h>
#include "page_table_inventory.h"
#define PAGE UINT64_C(4096)
#define ADDRESS_MASK UINT64_C(0x000ffffffffff000)
static int add(struct page_table_inventory *out, uint64_t p, uint8_t level)
{
    for (uint32_t i=0;i<out->count;i++) if (out->frame[i].physical==p) {
        if (out->frame[i].level!=level) return 0;
        out->shared++;
        return 2;
    }
    if (out->count==PAGE_TABLE_CAPACITY) return 0;
    out->frame[out->count++]=(struct page_table_frame){p,level};
    out->by_level[level]++;
    return 1;
}
int page_tables_walk(struct page_table_inventory *out, uint64_t cr3,
                     uint64_t cr0, uint64_t cr4, uint64_t efer,
                     unsigned bits, page_table_resolver resolve, void *ctx)
{
    if (!out || !resolve || bits<32 || bits>52 || !(cr0&(UINT64_C(1)<<31)) ||
        !(cr4&(UINT64_C(1)<<5)) || (cr4&(UINT64_C(1)<<12)) ||
        !(efer&(UINT64_C(1)<<10))) return 0;
    out->count=out->large_1g=out->large_2m=out->shared=0;
    for (unsigned i=0;i<5;i++) out->by_level[i]=0;
    uint64_t max=UINT64_C(1)<<bits;
    uint64_t root=cr3 & ADDRESS_MASK;
    if (!root || root>=max || (cr3 & UINT64_C(0x7ff)) & ~UINT64_C(0x18)) return 0;
    out->root=root;
    if (add(out,root,4)!=1) return 0;
    for (uint32_t index=0;index<out->count;index++) {
        uint64_t physical=out->frame[index].physical;
        unsigned level=out->frame[index].level;
        const volatile uint64_t *table=resolve(physical,ctx);
        if (!table) return 0;
        for (unsigned slot=0;slot<512;slot++) {
            uint64_t entry=table[slot];
            if (!(entry&1)) continue;
            uint64_t target=entry&ADDRESS_MASK;
            if (target>=max) return 0;
            if (level==4 && (entry&(UINT64_C(1)<<7))) return 0;
            if ((level==3 || level==2) && (entry&(UINT64_C(1)<<7))) {
                unsigned shift=level==3?30:21;
                uint64_t lowmask=(UINT64_C(1)<<shift)-1;
                if ((target & lowmask & ~UINT64_C(0x1000)) ||
                    (target & ~lowmask)>max-(UINT64_C(1)<<shift)) return 0;
                if (level==3) out->large_1g++; else out->large_2m++;
                continue;
            }
            if (level==1) continue;
            if (!target || add(out,target,(uint8_t)(level-1))==0) return 0;
        }
    }
    return 1;
}
const volatile uint64_t *page_table_hhdm_resolve(uint64_t p, void *context)
{
    const struct hhdm_table_context *ctx=context;
    if (!ctx || !ctx->map || p>UINT64_MAX-PAGE || p>UINT64_MAX-ctx->offset) return 0;
    for (uint64_t i=0;i<ctx->map->count;i++) {
        const struct memory_region *r=&ctx->map->region[i];
        if (r->type==LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE &&
            r->base<=p && p+PAGE<=r->end)
            return (const volatile uint64_t *)(uintptr_t)(ctx->offset+p);
    }
    return 0;
}
