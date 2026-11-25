#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "memory_reservations.h"
#include "page_table_inventory.h"
static struct reservation_inventory r;
static struct memory_inventory m;
static struct page_table_inventory t;
static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while (0)
static uint64_t page[4][512] __attribute__((aligned(4096)));
static const volatile uint64_t *resolve(uint64_t p,void *unused) {
    (void)unused;
    if(p>=0x1000 && p<=0x4000 && !(p&4095)) return page[(p>>12)-1];
    return 0;
}
static const volatile uint64_t *resolve_many(uint64_t p,void *context) {
    uint64_t (*pages)[512]=context;
    if (p<0x100000 || p>=0x100000+4609ULL*4096 || (p&4095)) return 0;
    return pages[(p-0x100000)/4096];
}
static void reset(void) {
    r=(struct reservation_inventory){0}; m=(struct memory_inventory){0};
    m.count=1; m.region[0]=(struct memory_region){.base=0,.end=64*4096,.type=0,.pages=64};
    m.candidates=64;
}
static uint32_t seed=0x12345678;
static uint32_t random32(void) { seed=seed*1664525u+1013904223u; return seed; }
int main(void) {
    reset(); CHECK(reservations_build(&r,&m)); CHECK(r.eligible_pages==64); CHECK(r.excluded_pages==0);
    CHECK(reservation_add(&r,4097,8191,RES_KERNEL,36));
    CHECK(reservation_add(&r,8192,12288,RES_IST,36));
    CHECK(reservation_add(&r,5000,6000,RES_DESCRIPTOR,36));
    CHECK(reservations_build(&r,&m)); CHECK(r.merged_count==1); CHECK(r.excluded_pages==2);
    CHECK(r.item_count==3); CHECK(r.eligible_count==2);
    reset(); CHECK(reservation_add(&r,0,64*4096,RES_KERNEL,36));
    CHECK(reservations_build(&r,&m)); CHECK(r.eligible_pages==0);
    reset(); CHECK(reservation_add(&r,70*4096,72*4096,RES_KERNEL,36));
    CHECK(reservations_build(&r,&m)); CHECK(r.eligible_pages==64);
    reset(); CHECK(!reservation_add(&r,0,0,RES_KERNEL,36));
    CHECK(!reservation_add(&r,4096,0,RES_KERNEL,36));
    CHECK(!reservation_add(&r,0,4096,0,36));
    CHECK(!reservation_add(&r,1ULL<<36,(1ULL<<36)+1,RES_KERNEL,36));
    for(unsigned trial=0;trial<200;trial++) {
        reset(); unsigned char excluded[64]={0};
        for(unsigned k=0;k<20;k++) {
            uint64_t a=random32()%(70*4096), b=a+1+random32()%(8*4096);
            CHECK(reservation_add(&r,a,b,RES_KERNEL,36));
            uint64_t lo=a/4096, hi=(b+4095)/4096;
            for(uint64_t i=lo;i<hi && i<64;i++) excluded[i]=1;
        }
        CHECK(reservations_build(&r,&m));
        unsigned expected=0; for(unsigned i=0;i<64;i++) expected+=!excluded[i];
        CHECK(r.eligible_pages==expected);
        CHECK(r.excluded_pages==64-expected);
    }
    reset();
    for(unsigned i=0;i<RESERVATION_CAPACITY;i++)
        CHECK(reservation_add(&r,4096,8192,RES_KERNEL,36));
    CHECK(!reservation_add(&r,4096,8192,RES_KERNEL,36));
    for(unsigned i=0;i<4;i++) for(unsigned j=0;j<512;j++) page[i][j]=0;
    page[0][0]=0x2003; page[1][0]=0x3003; page[2][0]=0x4003;
    page[2][1]=0x4003; /* shared PT */
    page[1][1]=0x40000083; /* 1 GiB leaf */
    page[2][2]=0x200083; /* 2 MiB leaf */
    CHECK(page_tables_walk(&t,0x1000,1ULL<<31,1ULL<<5,1ULL<<10,36,resolve,0));
    CHECK(t.count==4 && t.shared==1); CHECK(t.large_1g==1 && t.large_2m==1);
    CHECK(t.by_level[4]==1 && t.by_level[3]==1 && t.by_level[2]==1 && t.by_level[1]==1);
    CHECK(!page_tables_walk(&t,0x1000,0,1ULL<<5,1ULL<<10,36,resolve,0));
    CHECK(!page_tables_walk(&t,0x1000,1ULL<<31,(1ULL<<5)|(1ULL<<12),1ULL<<10,36,resolve,0));
    page[0][1]=0x81 | 0x3000; CHECK(!page_tables_walk(&t,0x1000,1ULL<<31,1ULL<<5,1ULL<<10,36,resolve,0));
    page[0][1]=0; page[1][2]=0x10000083; /* misaligned 1 GiB */
    CHECK(!page_tables_walk(&t,0x1000,1ULL<<31,1ULL<<5,1ULL<<10,36,resolve,0));
    page[1][2]=0; page[0][2]=0x5003; /* inaccessible table */
    CHECK(!page_tables_walk(&t,0x1000,1ULL<<31,1ULL<<5,1ULL<<10,36,resolve,0));
    page[0][2]=0; page[0][1]=0x1003; /* recursive alias at wrong level */
    CHECK(!page_tables_walk(&t,0x1000,1ULL<<31,1ULL<<5,1ULL<<10,36,resolve,0));
    uint64_t (*many)[512]=calloc(4609,sizeof(*many));
    CHECK(many!=0);
    for(unsigned i=0;i<512;i++) {
        many[0][i]=(0x100000ULL+(i+1ULL)*4096)|3;
        for(unsigned j=0;j<8;j++)
            many[i+1][j]=(0x100000ULL+(513ULL+i*8ULL+j)*4096)|3;
    }
    CHECK(!page_tables_walk(&t,0x100000,1ULL<<31,1ULL<<5,1ULL<<10,36,
                            resolve_many,many));
    free(many);
    printf("%u reservation and page-table checks passed\n",checks);
}
