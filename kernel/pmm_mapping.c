#include <stdint.h>
#include "pmm_mapping.h"
#define ADDRESS_MASK UINT64_C(0x000ffffffffff000)
static int canonical(uint64_t v)
{
    uint64_t upper=v>>48;
    return ((v>>47)&1) ? upper==UINT64_C(0xffff) : upper==0;
}
static int translate(uint64_t root, uint64_t virtual_address, unsigned bits,
                     page_table_resolver resolver, void *ctx, uint64_t *physical)
{
    uint64_t table_physical=root;
    uint64_t maximum=UINT64_C(1)<<bits;
    int user_accessible=1;
    for (unsigned level=4;level>=1;level--) {
        const volatile uint64_t *table=resolver(table_physical,ctx);
        if (!table) return 0;
        unsigned shift=12+9*(level-1);
        uint64_t entry=table[(virtual_address>>shift)&511];
        if (!(entry&1) || !(entry&2)) return 0;
        user_accessible &= !!(entry&4);
        uint64_t target=entry&ADDRESS_MASK;
        if (target>=maximum) return 0;
        if (level==4 && (entry&128)) return 0;
        if ((level==3 || level==2) && (entry&128)) {
            if (user_accessible) return 0;
            uint64_t size=UINT64_C(1)<<shift;
            if (target&(size-1)&~UINT64_C(0x1000)) return 0;
            target&=~(size-1);
            if (target>maximum-size) return 0;
            *physical=target+(virtual_address&(size-1));
            return 1;
        }
        if (level==1) {
            if (user_accessible) return 0;
            *physical=target+(virtual_address&4095);
            return 1;
        }
        if (!target) return 0;
        table_physical=target;
    }
    return 0;
}
int pmm_metadata_mapping_check(const struct pmm_plan *plan, uint64_t hhdm,
                               uint64_t cr3, unsigned bits,
                               page_table_resolver resolver, void *ctx,
                               uint64_t *verified_pages)
{
    if (!plan || !resolver || !verified_pages || bits<32 || bits>52 ||
        !plan->bitmap_storage_pages || plan->metadata_base>=plan->metadata_end ||
        (plan->metadata_base|plan->metadata_end)&4095 ||
        plan->metadata_end>UINT64_MAX-hhdm) return 0;
    *verified_pages=0;
    uint64_t root=cr3&ADDRESS_MASK;
    for (uint64_t p=plan->metadata_base;p<plan->metadata_end;p+=4096) {
        uint64_t virtual_address=hhdm+p;
        uint64_t actual;
        if (!canonical(virtual_address) ||
            !translate(root,virtual_address,bits,resolver,ctx,&actual) || actual!=p)
            return 0;
        (*verified_pages)++;
    }
    return *verified_pages==plan->bitmap_storage_pages;
}
