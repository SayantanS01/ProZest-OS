#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmm_alloc.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct reservation_inventory source;
static struct pmm_plan plan;
static void exercise(uint64_t pre,int fragmented)
{
    memset(&source,0,sizeof(source));
    source.eligible_count=1;
    source.eligible[0]=(struct physical_range){4096,4096+pre*4096};
    if (fragmented) {
        source.eligible_count=2;
        source.eligible[0].end=4096+pre/2*4096;
        source.eligible[1]=(struct physical_range){UINT64_C(0x100000000),UINT64_C(0x100000000)+(pre-pre/2)*4096};
    }
    source.eligible_pages=pre;
    source.merged_count=1;
    source.merged[0]=(struct physical_range){0,4096};
    CHECK(pmm_plan_build(&plan,&source,48));
    size_t bytes=(size_t)plan.bitmap_storage_pages*4096;
    uint8_t *raw=malloc(bytes+16),*saved=malloc(bytes);
    CHECK(raw && saved);
    memset(raw,0xa5,bytes+16);
    struct pmm_bitmap_state s={0};
    uint64_t out=UINT64_C(0xdeadbeef);
    CHECK(pmm_bitmap_alloc_page(&s,&out)==PMM_NOT_INITIALIZED && out==UINT64_C(0xdeadbeef));
    CHECK(pmm_bitmap_state_init(&s,&plan,&source,48,raw+8,bytes)==PMM_OK);
    CHECK(pmm_bitmap_alloc_page(&s,NULL)==PMM_INVALID_ADDRESS);
    CHECK(pmm_bitmap_alloc_page(&s,&s.stats.free_pages)==PMM_INVALID_ADDRESS);
    CHECK(pmm_bitmap_alloc_page(&s,(uint64_t *)(void *)(raw+8))==PMM_INVALID_ADDRESS);
    CHECK(pmm_bitmap_live_check(&s));
    uint64_t index=0;
    /* Independent reference enumerates original eligible intervals and skips metadata. */
    for (uint32_t r=0;r<source.eligible_count;r++) {
        for (uint64_t pa=source.eligible[r].base;pa<source.eligible[r].end;pa+=4096) {
            if (pa>=plan.metadata_base && pa<plan.metadata_end) continue;
            CHECK(pmm_bitmap_alloc_page(&s,&out)==PMM_OK && out==pa);
            CHECK(s.stats.allocated_pages==index+1 && s.stats.free_pages==plan.managed_pages-index-1);
            CHECK((raw[8+index/8]>>(index%8))&1u);
            ++index;
        }
    }
    CHECK(index==plan.managed_pages && pmm_bitmap_live_check(&s));
    memcpy(saved,raw+8,bytes);
    out=UINT64_C(0xdeadbeef);
    CHECK(pmm_bitmap_alloc_page(&s,&out)==PMM_OUT_OF_MEMORY && out==UINT64_C(0xdeadbeef));
    CHECK(!memcmp(saved,raw+8,bytes) && !s.stats.free_pages && s.stats.allocated_pages==index && !s.search_hint);
    CHECK(pmm_bitmap_state_init(&s,&plan,&source,48,raw+8,bytes)==PMM_ALREADY_INITIALIZED);
    CHECK(!memcmp(saved,raw+8,bytes));
    for (unsigned i=0;i<8;i++) CHECK(raw[i]==0xa5 && raw[bytes+8+i]==0xa5);
    /* Corrupt a padding bit and verify the live-state checker rejects it. */
    if (index<(uint64_t)bytes*8) {
        raw[8+index/8]&=(uint8_t)~(1u<<(index%8));
        CHECK(!pmm_bitmap_live_check(&s));
        memcpy(raw+8,saved,bytes);
    }
    CHECK(pmm_bitmap_live_check(&s));
    free(saved); free(raw);
}
int main(void)
{
    const uint64_t cases[]={2,8,9,10,17,100,32768,32769,65537};
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) exercise(cases[i],0);
    exercise(100,1); exercise(65537,1);
    printf("M10.5 allocation host assertions: %u/%u PASS\n",checks,checks);
    return 0;
}
