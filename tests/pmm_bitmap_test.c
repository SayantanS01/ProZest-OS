#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmm_bitmap.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct reservation_inventory source;
static struct pmm_plan plan;

static void exercise(uint64_t pre, int fragmented)
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
    CHECK(pmm_bitmap_plan_check(&plan,&source,48));
    size_t n=(size_t)plan.bitmap_storage_pages*4096;
    uint8_t *raw=malloc(n+2);
    CHECK(raw!=NULL);
    memset(raw,0xa5,n+2);
    struct pmm_bitmap_state state={0};
    CHECK(!pmm_bitmap_initial_check(&state));
    CHECK(pmm_bitmap_state_init(&state,&plan,&source,48,raw+1,n-1)==PMM_INVALID_ADDRESS);
    CHECK(!state.initialized && raw[1]==0xa5 && raw[n]==0xa5);
    uint64_t saved=plan.bitmap_required_bytes;
    plan.bitmap_required_bytes++;
    CHECK(pmm_bitmap_state_init(&state,&plan,&source,48,raw+1,n)==PMM_INVALID_ADDRESS);
    CHECK(!state.initialized && raw[1]==0xa5);
    plan.bitmap_required_bytes=saved;
    source.eligible_count=ELIGIBLE_CAPACITY+1;
    CHECK(!pmm_bitmap_plan_check(&plan,&source,48));
    source.eligible_count=fragmented?2:1;
    source.merged[0]=(struct physical_range){4096,8192};
    CHECK(!pmm_bitmap_plan_check(&plan,&source,48));
    source.merged[0]=(struct physical_range){0,4096};
    CHECK(pmm_bitmap_state_init(&state,&plan,&source,48,raw+1,n)==PMM_OK);
    CHECK(state.initialized && state.stats.managed_pages==plan.managed_pages);
    CHECK(state.stats.free_pages==plan.managed_pages && !state.stats.allocated_pages && !state.search_hint);
    CHECK(raw[0]==0xa5 && raw[n+1]==0xa5);
    /* Independent per-bit reference, including every padding bit. */
    for (uint64_t i=0;i<(uint64_t)n*8;i++)
        CHECK(((raw[1+i/8]>>(i%8))&1u)==(i>=plan.managed_pages));
    CHECK(pmm_bitmap_initial_check(&state));
    raw[1]^=1;
    CHECK(!pmm_bitmap_initial_check(&state));
    CHECK(pmm_bitmap_state_init(&state,&plan,&source,48,raw+1,n)==PMM_ALREADY_INITIALIZED);
    CHECK(raw[1]&1u); /* Reinitialization must not clear a live bit. */
    raw[1]^=1;
    CHECK(pmm_bitmap_initial_check(&state));
    CHECK(!pmm_is_managed(&plan,plan.metadata_base));
    CHECK(pmm_is_managed(&plan,plan.range[0].base));
    free(raw);
}
int main(void)
{
    const uint64_t cases[]={2,8,9,10,17,32768,32769,32770,65536,65537};
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) exercise(cases[i],0);
    exercise(100,1);
    exercise(65537,1);
    printf("M10.4 bitmap host checks: %u/%u PASS\n",checks,checks);
    return 0;
}
