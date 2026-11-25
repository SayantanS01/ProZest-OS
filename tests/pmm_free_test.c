#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmm_free.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct reservation_inventory source;
static struct pmm_plan plan;
static uint32_t rng=UINT32_C(0x9123abcd);
static uint32_t next_random(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static void unchanged_free(struct pmm_bitmap_state *s, uint64_t pa, enum pmm_status status, uint8_t *saved)
{
    struct pmm_bitmap_state before=*s;
    memcpy(saved,(const void *)s->bitmap,s->storage_bytes);
    CHECK(pmm_bitmap_free_page(s,pa)==status);
    CHECK(s->plan==before.plan && s->bitmap==before.bitmap && s->storage_bytes==before.storage_bytes &&
          s->initialized==before.initialized && s->search_hint==before.search_hint &&
          s->stats.managed_pages==before.stats.managed_pages && s->stats.free_pages==before.stats.free_pages &&
          s->stats.allocated_pages==before.stats.allocated_pages);
    CHECK(!memcmp(saved,(const void *)s->bitmap,s->storage_bytes));
}
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
    source.merged_count=1; source.merged[0]=(struct physical_range){0,4096};
    CHECK(pmm_plan_build(&plan,&source,48));
    size_t bytes=(size_t)plan.bitmap_storage_pages*4096;
    uint64_t n=plan.managed_pages;
    uint8_t *raw=malloc(bytes+16),*saved=malloc(bytes),*model=calloc((size_t)n,1);
    uint64_t *address=malloc((size_t)n*sizeof(*address));
    CHECK(raw && saved && model && address);
    memset(raw,0xa5,bytes+16);
    struct pmm_bitmap_state s={0};
    CHECK(pmm_bitmap_free_page(NULL,4096)==PMM_NOT_INITIALIZED);
    CHECK(pmm_bitmap_free_page(&s,4096)==PMM_NOT_INITIALIZED);
    CHECK(pmm_bitmap_state_init(&s,&plan,&source,48,raw+8,bytes)==PMM_OK);
    uint64_t k=0;
    for (uint32_t r=0;r<source.eligible_count;r++)
        for (uint64_t pa=source.eligible[r].base;pa<source.eligible[r].end;pa+=4096)
            if (!(pa>=plan.metadata_base && pa<plan.metadata_end)) { CHECK(k<n); address[k++]=pa; }
    CHECK(k==n);
    unchanged_free(&s,0,PMM_RESERVED,saved);
    unchanged_free(&s,plan.metadata_base,PMM_RESERVED,saved);
    unchanged_free(&s,address[0]+1,PMM_INVALID_ADDRESS,saved);
    unchanged_free(&s,address[0],PMM_DOUBLE_FREE,saved);
    unchanged_free(&s,UINT64_C(0x80000000),PMM_RESERVED,saved);
    unchanged_free(&s,UINT64_MAX,PMM_INVALID_ADDRESS,saved);
    uint64_t out=0;
    for (uint64_t i=0;i<n;i++) CHECK(pmm_bitmap_alloc_page(&s,&out)==PMM_OK && out==address[i]);
    CHECK(pmm_bitmap_live_check(&s) && !s.stats.free_pages);
    /* Exhaustion, then free and reuse the last frame, including a high fragmented range. */
    CHECK(pmm_bitmap_free_page(&s,address[n-1])==PMM_OK && s.stats.free_pages==1);
    unchanged_free(&s,address[n-1],PMM_DOUBLE_FREE,saved);
    CHECK(pmm_bitmap_alloc_page(&s,&out)==PMM_OK && out==address[n-1] && !s.stats.free_pages);
    for (uint64_t i=n;i>0;i--) {
        CHECK(pmm_bitmap_free_page(&s,address[i-1])==PMM_OK);
        CHECK(s.stats.allocated_pages==i-1 && s.stats.free_pages==n-i+1);
        CHECK(!(raw[8+(i-1)/8]&(1u<<((i-1)%8))));
    }
    CHECK(pmm_bitmap_initial_check(&s)); /* Last free was index zero. */
    /* Deterministic randomized operations against a separate byte-per-page model. */
    uint64_t hint=0,used=0;
    for (unsigned step=0;step<2000;step++) {
        if (next_random()&1u) {
            uint64_t i=next_random()%n;
            if (!model[i]) unchanged_free(&s,address[i],PMM_DOUBLE_FREE,saved);
            else { CHECK(pmm_bitmap_free_page(&s,address[i])==PMM_OK); model[i]=0; --used; hint=i; }
        } else {
            if (used==n) {
                out=UINT64_MAX; CHECK(pmm_bitmap_alloc_page(&s,&out)==PMM_OUT_OF_MEMORY && out==UINT64_MAX);
            } else {
                uint64_t i=hint;
                while (model[i]) i=(i+1==n)?0:i+1;
                CHECK(pmm_bitmap_alloc_page(&s,&out)==PMM_OK && out==address[i]);
                model[i]=1; ++used; hint=(i+1==n)?0:i+1;
            }
        }
        CHECK(s.stats.allocated_pages==used && s.stats.free_pages==n-used && s.search_hint==hint);
        if (step%101==0) CHECK(pmm_bitmap_live_check(&s));
    }
    for (uint64_t i=0;i<n;i++) CHECK(((raw[8+i/8]>>(i%8))&1u)==model[i]);
    CHECK(pmm_bitmap_live_check(&s));
    for (unsigned i=0;i<8;i++) CHECK(raw[i]==0xa5 && raw[bytes+8+i]==0xa5);
    free(address); free(model); free(saved); free(raw);
}
int main(void)
{
    const uint64_t cases[]={2,8,9,17,100,32768,32769,65537};
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) exercise(cases[i],0);
    exercise(100,1); exercise(65537,1);
    printf("M10.6 free/reuse host assertions: %u/%u PASS\n",checks,checks);
    return 0;
}
