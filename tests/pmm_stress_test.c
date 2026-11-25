#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmm_contiguous.h"
#define P UINT64_C(4096)
#define MAX 4096u
#define SLOTS 128u
#define STEPS 20000u
static unsigned long long checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static struct reservation_inventory source;
static struct pmm_plan plan;
static struct pmm_bitmap_state s;
static _Alignas(8) uint8_t raw[4096+16], saved[sizeof(raw)];
static uint64_t addr[MAX];
static uint8_t used[MAX];
static struct slot { unsigned index,count,align,kind; } slots[SLOTS];
static uint32_t rng;
static uint32_t random_word(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static unsigned n,hint,occupied;
static int candidate(unsigned i,unsigned count,unsigned align) {
    if (count>n-i || (addr[i]/P)%align) return 0;
    for (unsigned k=0;k<count;k++) if (used[i+k] || addr[i+k]!=addr[i]+k*P) return 0;
    return 1;
}
static void verify(void) {
    struct pmm_stats st;
    CHECK(pmm_bitmap_get_stats(&s,&st)==PMM_OK && st.managed_pages==n && st.allocated_pages==occupied && st.free_pages==n-occupied);
    CHECK(s.search_hint==hint && pmm_bitmap_live_check(&s));
    for (unsigned i=0;i<4096;i++) {
        unsigned expected=0;
        for (unsigned b=0;b<8;b++) if (i*8+b>=n || used[i*8+b]) expected|=1u<<b;
        CHECK(raw[8+i]==expected);
    }
    for (unsigned i=0;i<8;i++) CHECK(raw[i]==0xa5 && raw[4104+i]==0xa5);
}
static void release(struct slot *q) {
    CHECK(q->count && (q->kind==1?pmm_bitmap_free_page(&s,addr[q->index]):pmm_bitmap_free_contiguous(&s,addr[q->index],q->count))==PMM_OK);
    for (unsigned k=0;k<q->count;k++) { CHECK(used[q->index+k]); used[q->index+k]=0; }
    occupied-=q->count; hint=q->index; q->count=0;
}
static void run(unsigned a,unsigned b,uint32_t seed) {
    memset(&source,0,sizeof(source)); memset(&plan,0,sizeof(plan)); memset(&s,0,sizeof(s));
    memset(raw,0xa5,sizeof(raw)); memset(used,0,sizeof(used)); memset(slots,0,sizeof(slots));
    source.eligible_count=2; source.eligible[0]=(struct physical_range){P,(a+2)*P};
    source.eligible[1]=(struct physical_range){UINT64_C(0x100000000),UINT64_C(0x100000000)+b*P};
    source.eligible_pages=a+b+1; source.merged_count=1; source.merged[0]=(struct physical_range){0,P};
    n=a+b; hint=occupied=0; rng=seed;
    CHECK(n<=MAX && pmm_plan_build(&plan,&source,48) && plan.managed_pages==n);
    CHECK(pmm_bitmap_state_init(&s,&plan,&source,48,raw+8,4096)==PMM_OK);
    for (unsigned i=0;i<n;i++) { addr[i]=i<a?(i+2)*P:UINT64_C(0x100000000)+(i-a)*P; uint64_t pa=0; CHECK(pmm_index_to_page(&plan,i,&pa)&&pa==addr[i]); }
    unsigned allocs=0,frees=0,oom=0;
    for (unsigned step=0;step<STEPS;step++) {
        struct slot *q=&slots[random_word()%SLOTS];
        if (q->count) { release(q); ++frees; }
        else {
            unsigned kind=1+(random_word()&1u),count=1,align=1;
            if (kind==2) { count=1+random_word()%16; align=1u<<(random_word()%5); }
            unsigned first=n;
            for (unsigned k=0;k<n;k++) { unsigned i=kind==1?(hint+k)%n:k; if (candidate(i,count,align)) { first=i; break; } }
            uint64_t pa=0xfeedface; struct pmm_bitmap_state before=s;
            if (first==n) memcpy(saved,raw,sizeof(raw));
            enum pmm_status st=kind==1?pmm_bitmap_alloc_page(&s,&pa):pmm_bitmap_alloc_contiguous(&s,count,align,&pa);
            if (first==n) { CHECK(st==PMM_OUT_OF_MEMORY && pa==0xfeedface && !memcmp(&s,&before,sizeof(s)) && !memcmp(saved,raw,sizeof(raw))); ++oom; }
            else { CHECK(st==PMM_OK && pa==addr[first]); for (unsigned k=0;k<count;k++) { CHECK(!used[first+k]); used[first+k]=1; } occupied+=count; hint=(first+count)%n; *q=(struct slot){first,count,align,kind}; ++allocs; }
        }
        CHECK(s.stats.allocated_pages==occupied && s.stats.free_pages==n-occupied && s.search_hint==hint);
        if ((step&63u)==63u) verify();
    }
    for (unsigned j=0;j<SLOTS;j++) if (slots[j].count) { release(&slots[j]); ++frees; }
    CHECK(allocs==frees && !occupied); verify();
    if (n<128) CHECK(oom>0);
    printf("Fixture %u pages seed 0x%08x: %u steps, %u allocs, %u frees, %u OOM, zero live pages\n",n,seed,STEPS,allocs,frees,oom);
}
int main(void) {
    run(16,9,0x93a4b17d); run(63,17,0x12345678); run(1023,1025,0xdeadbeef); run(2047,2049,0x7f4a7c15);
    printf("M10.9 stress host assertions: %llu/%llu PASS\n",checks,checks);
    return 0;
}
