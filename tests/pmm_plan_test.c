#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "pmm_plan.h"
#include "pmm_mapping.h"
static struct reservation_inventory source;
static struct pmm_plan plan;
static uint64_t table[4][512] __attribute__((aligned(4096)));
static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static const volatile uint64_t *resolve(uint64_t p,void *ctx) {
    (void)ctx;
    if (p>=0x1000 && p<=0x4000 && !(p&4095)) return table[(p>>12)-1];
    return 0;
}
static uint32_t seed=0x9e3779b9;
static uint32_t rand32(void) {seed=seed*1664525u+1013904223u;return seed;}
static void reset(void) {
    source=(struct reservation_inventory){0}; plan=(struct pmm_plan){0};
    source.eligible_count=3;
    source.eligible[0]=(struct physical_range){0x1000,0x3000};
    source.eligible[1]=(struct physical_range){0x10000,0x14000};
    source.eligible[2]=(struct physical_range){0x100000,0x105000};
    source.eligible_pages=11;
}
int main(void) {
    uint64_t bytes=0;
    for(uint64_t n=0;n<=9;n++) { CHECK(pmm_bitmap_bytes(n,&bytes)); CHECK(bytes==(n+7)/8); }
    CHECK(pmm_bitmap_bytes(UINT64_MAX,&bytes)); CHECK(bytes==UINT64_MAX/8+1);
    CHECK(!pmm_bitmap_bytes(1,0));
    reset(); CHECK(pmm_plan_build(&plan,&source,36));
    CHECK(plan.metadata_base==0x1000 && plan.metadata_end==0x2000);
    CHECK(plan.bitmap_required_bytes==2 && plan.bitmap_storage_pages==1);
    CHECK(plan.managed_pages==10 && plan.range_count==3);
    CHECK(plan.range[0].base==0x2000 && plan.range[1].first_index==1);
    CHECK(plan.unused_trailing_bits==32768-11);
    CHECK(plan.metadata_total_bytes==sizeof(plan)+4096);
    uint64_t v=0;
    CHECK(!pmm_page_to_index(&plan,0x1000,&v));
    CHECK(!pmm_page_to_index(&plan,0x3000,&v));
    CHECK(!pmm_page_to_index(&plan,0x10001,&v));
    CHECK(!pmm_index_to_page(&plan,10,&v));
    for(uint64_t i=0;i<plan.managed_pages;i++) {
        uint64_t p,index;
        CHECK(pmm_index_to_page(&plan,i,&p));
        CHECK(pmm_page_to_index(&plan,p,&index));
        CHECK(index==i);
    }
    reset(); source.eligible_count=2;
    source.eligible[0]=(struct physical_range){0x1000,0x2000};
    source.eligible[1]=(struct physical_range){0x10000,0x20000};
    source.eligible_pages=17;
    CHECK(pmm_plan_build(&plan,&source,36)); CHECK(plan.range_count==1);
    CHECK(plan.range[0].base==0x10000 && plan.metadata_end==0x2000);
    reset(); source.eligible_count=1; source.eligible_pages=1;
    source.eligible[0]=(struct physical_range){0x1000,0x2000};
    CHECK(!pmm_plan_build(&plan,&source,36));
    reset(); CHECK(!pmm_plan_build(&plan,&source,31));
    CHECK(!pmm_plan_build(&plan,&source,53));
    source.eligible[1].base++; CHECK(!pmm_plan_build(&plan,&source,36));
    reset(); source.eligible_count=513; CHECK(!pmm_plan_build(&plan,&source,36));
    for(unsigned trial=0;trial<200;trial++) {
        reset(); source.eligible_count=1+rand32()%12;
        uint64_t cursor=4096,total=0;
        for(uint32_t i=0;i<source.eligible_count;i++) {
            cursor+=(rand32()%8)*4096;
            uint64_t pages=2+rand32()%128;
            source.eligible[i]=(struct physical_range){cursor,cursor+pages*4096};
            cursor+=pages*4096; total+=pages;
        }
        source.eligible_pages=total;
        CHECK(pmm_plan_build(&plan,&source,36));
        CHECK(pmm_plan_validate(&plan,&source,36));
        CHECK(plan.managed_pages+plan.bitmap_storage_pages==total);
        for(uint32_t i=0;i<plan.range_count;i++) {
            uint64_t p,index,back;
            p=plan.range[i].base; CHECK(pmm_page_to_index(&plan,p,&index));
            CHECK(pmm_index_to_page(&plan,index,&back)); CHECK(back==p);
            p=plan.range[i].end-4096; CHECK(pmm_page_to_index(&plan,p,&index));
            CHECK(pmm_index_to_page(&plan,index,&back)); CHECK(back==p);
        }
        CHECK(!pmm_is_managed(&plan,plan.metadata_base));
    }
    reset(); CHECK(pmm_plan_build(&plan,&source,36));
    for(unsigned i=0;i<4;i++) for(unsigned j=0;j<512;j++) table[i][j]=0;
    table[0][256]=0x2007; table[1][0]=0x3007; table[2][0]=0x4007;
    table[3][1]=0x1003;
    uint64_t verified=0;
    CHECK(pmm_metadata_mapping_check(&plan,0xffff800000000000ULL,0x1000,36,resolve,0,&verified));
    CHECK(verified==1);
    table[3][1]=0x1001; CHECK(!pmm_metadata_mapping_check(&plan,0xffff800000000000ULL,0x1000,36,resolve,0,&verified));
    table[3][1]=0x2003; CHECK(!pmm_metadata_mapping_check(&plan,0xffff800000000000ULL,0x1000,36,resolve,0,&verified));
    table[2][0]=0x83; CHECK(pmm_metadata_mapping_check(&plan,0xffff800000000000ULL,0x1000,36,resolve,0,&verified));
    table[1][0]=0x81; CHECK(!pmm_metadata_mapping_check(&plan,0xffff800000000000ULL,0x1000,36,resolve,0,&verified));
    printf("%u PMM plan checks passed\n",checks);
}
