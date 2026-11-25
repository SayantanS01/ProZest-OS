#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmm_safety.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct reservation_inventory source;
static struct pmm_plan plan;
static struct pmm_bitmap_state s;
static _Alignas(8) uint8_t raw[4096+16], saved[4096];
static struct pmm_bitmap_state before;
static struct pmm_plan saved_plan;
static void snapshot(void) {
    before=s; saved_plan=plan; memcpy(saved,raw+8,4096);
}
static void unchanged(void) {
    CHECK(s.plan==before.plan && s.bitmap==before.bitmap && s.storage_bytes==before.storage_bytes &&
          s.initialized==before.initialized && s.search_hint==before.search_hint &&
          s.stats.managed_pages==before.stats.managed_pages && s.stats.free_pages==before.stats.free_pages &&
          s.stats.allocated_pages==before.stats.allocated_pages);
    CHECK(!memcmp(&plan,&saved_plan,sizeof(plan)) && !memcmp(saved,raw+8,4096));
    for (unsigned i=0;i<8;i++) CHECK(raw[i]==0xa5 && raw[4104+i]==0xa5);
}
#define REJECT(expr,status) do { snapshot(); CHECK((expr)==(status)); unchanged(); } while(0)
static void corrupt_reject(void) {
    uint64_t out=UINT64_C(0xdeadbeef);
    struct pmm_stats stats={1,2,3};
    CHECK(!pmm_bitmap_live_check(&s));
    REJECT(pmm_bitmap_alloc_page(&s,&out),PMM_UNSUPPORTED);
    CHECK(out==UINT64_C(0xdeadbeef));
    REJECT(pmm_bitmap_free_page(&s,8192),PMM_UNSUPPORTED);
    REJECT(pmm_bitmap_get_stats(&s,&stats),PMM_UNSUPPORTED);
    CHECK(stats.managed_pages==1 && stats.free_pages==2 && stats.allocated_pages==3);
}
int main(void) {
    source.eligible_count=2;
    source.eligible[0]=(struct physical_range){4096,4096+17*4096};
    source.eligible[1]=(struct physical_range){UINT64_C(0x100000000),UINT64_C(0x100000000)+9*4096};
    source.eligible_pages=26;
    source.merged_count=2;
    source.merged[0]=(struct physical_range){0,4096};
    source.merged[1]=(struct physical_range){UINT64_C(0x200000),UINT64_C(0x204000)};
    CHECK(pmm_plan_build(&plan,&source,48) && plan.managed_pages==25);
    memset(raw,0xa5,sizeof(raw));
    uint64_t out=UINT64_C(0xdeadbeef), address[25];
    struct pmm_stats stats={1,2,3};
    REJECT(pmm_bitmap_get_stats(&s,&stats),PMM_NOT_INITIALIZED);
    CHECK(stats.managed_pages==1 && stats.free_pages==2 && stats.allocated_pages==3);
    REJECT(pmm_bitmap_alloc_page(&s,&out),PMM_NOT_INITIALIZED);
    CHECK(out==UINT64_C(0xdeadbeef));
    REJECT(pmm_bitmap_free_page(&s,8192),PMM_NOT_INITIALIZED);
    CHECK(pmm_bitmap_state_init(&s,&plan,&source,48,raw+8,4096)==PMM_OK);
    CHECK(pmm_bitmap_get_stats(&s,&stats)==PMM_OK && stats.free_pages==25 && !stats.allocated_pages);
    for (uint64_t i=0;i<25;i++) CHECK(pmm_index_to_page(&plan,i,&address[i]));
    REJECT(pmm_bitmap_alloc_page(&s,NULL),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_alloc_page(&s,(uint64_t *)(void *)(raw+9)),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_alloc_page(&s,&s.search_hint),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_alloc_page(&s,(uint64_t *)(void *)&plan),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_alloc_page(&s,(uint64_t *)(void *)(raw+8)),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_get_stats(&s,NULL),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_get_stats(&s,(struct pmm_stats *)(void *)(raw+9)),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_get_stats(&s,&s.stats),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_get_stats(&s,(struct pmm_stats *)(void *)&plan),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_get_stats(&s,(struct pmm_stats *)(void *)(raw+8)),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_get_stats(&s,(struct pmm_stats *)(void *)(raw+4096)),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_free_page(&s,0),PMM_RESERVED);
    REJECT(pmm_bitmap_free_page(&s,4096),PMM_RESERVED);
    REJECT(pmm_bitmap_free_page(&s,UINT64_C(0x200000)),PMM_RESERVED);
    REJECT(pmm_bitmap_free_page(&s,UINT64_C(0x80000000)),PMM_RESERVED);
    REJECT(pmm_bitmap_free_page(&s,UINT64_C(1)<<48),PMM_RESERVED);
    REJECT(pmm_bitmap_free_page(&s,UINT64_MAX),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_free_page(&s,address[0]+1),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_free_page(&s,address[0]),PMM_DOUBLE_FREE);
    for (uint64_t i=0;i<25;i++) CHECK(pmm_bitmap_alloc_page(&s,&out)==PMM_OK && out==address[i]);
    out=UINT64_C(0xdeadbeef);
    REJECT(pmm_bitmap_alloc_page(&s,&out),PMM_OUT_OF_MEMORY);
    CHECK(out==UINT64_C(0xdeadbeef));
    REJECT(pmm_bitmap_state_init(&s,NULL,NULL,0,NULL,0),PMM_ALREADY_INITIALIZED);
    CHECK(pmm_bitmap_free_page(&s,address[24])==PMM_OK);
    REJECT(pmm_bitmap_free_page(&s,address[24]),PMM_DOUBLE_FREE);
    CHECK(pmm_bitmap_alloc_page(&s,&out)==PMM_OK && out==address[24]);
    CHECK(pmm_bitmap_live_check(&s));
    /* Isolated malformed state/plan tests. Restore after each mutation. */
    s.search_hint=25; corrupt_reject(); s.search_hint=0;
    s.stats.free_pages=1; corrupt_reject(); s.stats.free_pages=0;
    s.storage_bytes=4095; corrupt_reject(); s.storage_bytes=4096;
    s.initialized=2; corrupt_reject(); s.initialized=1;
    plan.range[1].first_index++; corrupt_reject(); plan.range[1].first_index--;
    plan.range[0].base--; corrupt_reject(); plan.range[0].base++;
    plan.metadata_end+=4096; corrupt_reject(); plan.metadata_end-=4096;
    uint64_t original=plan.range[1].base; plan.range[1].base=plan.range[0].base;
    corrupt_reject(); plan.range[1].base=original;
    uint32_t count=plan.range_count; plan.range_count=PMM_PLAN_RANGE_CAPACITY+1;
    corrupt_reject(); plan.range_count=count;
    /* Full bitmap audit detects corrupt padding and counter/bit mismatch.
     * Fast allocation/free paths do not scan every bit on each request. */
    raw[8+25/8]&=(uint8_t)~(1u<<(25%8)); CHECK(!pmm_bitmap_live_check(&s));
    raw[8+25/8]|=(uint8_t)(1u<<(25%8)); CHECK(pmm_bitmap_live_check(&s));
    raw[8]&=(uint8_t)~1u; CHECK(!pmm_bitmap_live_check(&s));
    raw[8]|=1u; CHECK(pmm_bitmap_live_check(&s));
    for (uint64_t i=25;i>0;i--) CHECK(pmm_bitmap_free_page(&s,address[i-1])==PMM_OK);
    CHECK(pmm_bitmap_initial_check(&s));
    printf("M10.7 safety host assertions: %u/%u PASS\n",checks,checks);
    return 0;
}
