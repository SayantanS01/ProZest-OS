#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmm_contiguous.h"
#define P UINT64_C(4096)
#define N 25
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct reservation_inventory source;
static struct pmm_plan plan;
static struct pmm_bitmap_state s;
static _Alignas(8) uint8_t raw[4096+16], saved[sizeof(raw)];
static uint64_t addr[N];
static unsigned used[N];
static struct pmm_bitmap_state before;
static struct pmm_plan old_plan;
static void snapshot(void) { before=s; old_plan=plan; memcpy(saved,raw,sizeof(raw)); }
static void unchanged(void) {
    CHECK(!memcmp(&s,&before,sizeof(s)) && !memcmp(&plan,&old_plan,sizeof(plan)) && !memcmp(raw,saved,sizeof(raw)));
}
#define REJECT(expr,status) do { snapshot(); CHECK((expr)==(status)); unchanged(); } while(0)
static void verify(void) {
    uint64_t n=0;
    for (unsigned i=0;i<N;i++) { CHECK(((raw[8+i/8]>>(i%8))&1u)==used[i]); n+=used[i]; }
    CHECK(s.stats.allocated_pages==n && s.stats.free_pages==N-n && s.stats.managed_pages==N);
    CHECK(s.search_hint<N && pmm_bitmap_live_check(&s));
    for (unsigned i=0;i<8;i++) CHECK(raw[i]==0xa5 && raw[4104+i]==0xa5);
}
static unsigned rng=0x71acbd29u;
static unsigned random_word(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
/* Independent physical-address reference; fixtures have a genuine gap. */
static int candidate(unsigned i,unsigned count,unsigned align) {
    if (i+count>N || (addr[i]/P)%align) return 0;
    for (unsigned k=0;k<count;k++) if (used[i+k] || addr[i+k]!=addr[i]+k*P) return 0;
    return 1;
}
int main(void) {
    uint64_t out=0xfeedface;
    REJECT(pmm_bitmap_alloc_contiguous(&s,1,1,&out),PMM_NOT_INITIALIZED);
    REJECT(pmm_bitmap_free_contiguous(&s,P,1),PMM_NOT_INITIALIZED);
    source.eligible_count=2;
    source.eligible[0]=(struct physical_range){P,18*P};
    source.eligible[1]=(struct physical_range){UINT64_C(0x100000000),UINT64_C(0x100000000)+9*P};
    source.eligible_pages=26;
    source.merged_count=1; source.merged[0]=(struct physical_range){0,P};
    CHECK(pmm_plan_build(&plan,&source,48) && plan.managed_pages==N);
    memset(raw,0xa5,sizeof(raw));
    CHECK(pmm_bitmap_state_init(&s,&plan,&source,48,raw+8,4096)==PMM_OK);
    for (unsigned i=0;i<N;i++) CHECK(pmm_index_to_page(&plan,i,&addr[i]));
    REJECT(pmm_bitmap_alloc_contiguous(&s,0,1,&out),PMM_INVALID_COUNT);
    REJECT(pmm_bitmap_alloc_contiguous(&s,1,0,&out),PMM_INVALID_COUNT);
    REJECT(pmm_bitmap_alloc_contiguous(&s,1,3,&out),PMM_INVALID_COUNT);
    REJECT(pmm_bitmap_alloc_contiguous(&s,UINT64_MAX,1,&out),PMM_INVALID_COUNT);
    REJECT(pmm_bitmap_alloc_contiguous(&s,1,UINT64_C(1)<<63,&out),PMM_INVALID_COUNT);
    REJECT(pmm_bitmap_alloc_contiguous(&s,1,1,NULL),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_alloc_contiguous(&s,1,1,&s.search_hint),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_alloc_contiguous(&s,1,1,(uint64_t *)(void *)&plan),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_alloc_contiguous(&s,1,1,(uint64_t *)(void *)(raw+8)),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_alloc_contiguous(&s,1,1,(uint64_t *)(void *)(raw+9)),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_alloc_contiguous(&s,17,1,&out),PMM_OUT_OF_MEMORY);
    REJECT(pmm_bitmap_alloc_contiguous(&s,1,UINT64_C(1)<<40,&out),PMM_OUT_OF_MEMORY);
    CHECK(out==0xfeedface);
    REJECT(pmm_bitmap_free_contiguous(&s,0,1),PMM_RESERVED);
    REJECT(pmm_bitmap_free_contiguous(&s,plan.metadata_base,1),PMM_RESERVED);
    REJECT(pmm_bitmap_free_contiguous(&s,addr[15],2),PMM_RESERVED);
    REJECT(pmm_bitmap_free_contiguous(&s,addr[24],2),PMM_RESERVED);
    REJECT(pmm_bitmap_free_contiguous(&s,addr[0]+1,1),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_free_contiguous(&s,UINT64_MAX-P+1,1),PMM_INVALID_ADDRESS);
    REJECT(pmm_bitmap_free_contiguous(&s,addr[0],0),PMM_INVALID_COUNT);
    REJECT(pmm_bitmap_free_contiguous(&s,addr[0],UINT64_MAX),PMM_INVALID_COUNT);
    REJECT(pmm_bitmap_free_contiguous(&s,addr[0],1),PMM_DOUBLE_FREE);
    CHECK(pmm_bitmap_alloc_contiguous(&s,8,8,&out)==PMM_OK && out==8*P);
    for (unsigned i=6;i<14;i++) used[i]=1;
    verify();
    CHECK(pmm_bitmap_free_page(&s,9*P)==PMM_OK); used[7]=0;
    REJECT(pmm_bitmap_free_contiguous(&s,8*P,8),PMM_DOUBLE_FREE);
    verify();
    CHECK(pmm_bitmap_alloc_page(&s,&out)==PMM_OK && out==9*P); used[7]=1;
    CHECK(pmm_bitmap_free_contiguous(&s,8*P,8)==PMM_OK);
    memset(used,0,sizeof(used)); verify();
    for (unsigned iter=0;iter<12000;iter++) {
        unsigned count=1+random_word()%12, align=1u<<(random_word()%5), i=random_word()%N;
        if (random_word()&1) {
            int first=-1;
            for (unsigned j=0;j<N;j++) if (candidate(j,count,align)) { first=(int)j; break; }
            out=0xfeedface;
            if (first<0) { REJECT(pmm_bitmap_alloc_contiguous(&s,count,align,&out),PMM_OUT_OF_MEMORY); CHECK(out==0xfeedface); }
            else {
                CHECK(pmm_bitmap_alloc_contiguous(&s,count,align,&out)==PMM_OK && out==addr[first]);
                for (unsigned k=0;k<count;k++) used[first+k]=1;
            }
        } else {
            int inside=i+count<=N && addr[i+count-1]==addr[i]+(count-1)*P;
            int all=inside;
            if (inside) for (unsigned k=0;k<count;k++) if (!used[i+k]) all=0;
            if (!inside) { REJECT(pmm_bitmap_free_contiguous(&s,addr[i],count),PMM_RESERVED); }
            else if (!all) { REJECT(pmm_bitmap_free_contiguous(&s,addr[i],count),PMM_DOUBLE_FREE); }
            else { CHECK(pmm_bitmap_free_contiguous(&s,addr[i],count)==PMM_OK); for (unsigned k=0;k<count;k++) used[i+k]=0; }
        }
        verify();
    }
    for (unsigned i=0;i<N;i++) if (used[i]) { CHECK(pmm_bitmap_free_page(&s,addr[i])==PMM_OK); used[i]=0; }
    verify();
    CHECK(pmm_bitmap_alloc_contiguous(&s,16,1,&out)==PMM_OK && out==addr[0]);
    CHECK(pmm_bitmap_alloc_contiguous(&s,9,1,&out)==PMM_OK && out==addr[16]);
    REJECT(pmm_bitmap_alloc_contiguous(&s,1,1,&out),PMM_OUT_OF_MEMORY);
    CHECK(pmm_bitmap_free_contiguous(&s,addr[0],16)==PMM_OK && pmm_bitmap_free_contiguous(&s,addr[16],9)==PMM_OK);
    verify();
    printf("M10.8 contiguous host assertions: %u/%u PASS\n",checks,checks);
    return 0;
}
