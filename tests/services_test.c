#include "kheap.h"
#include "device_core.h"
#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
static unsigned long checks;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static alignas(16) unsigned char arena[65536];
static struct kheap h;
static unsigned char used[sizeof(arena)/16];
static uint32_t rng=0x12345678;
static uint32_t rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static struct {unsigned char *p;size_t n;unsigned char tag;} slots[64];
static void mark(unsigned char *p,size_t n,unsigned char old,unsigned char new){size_t a=(size_t)(p-arena)/16,b=(n+15)/16;C(a+b<=sizeof(used));for(size_t j=a;j<a+b;j++){C(used[j]==old);used[j]=new;}}
static void heap_test(void){
    C(!kheap_alloc(&h,1));C(!kheap_init(&h,arena+1,16));C(!kheap_init(&h,arena,15));C(kheap_init(&h,arena,sizeof(arena)));C(!kheap_init(&h,arena,sizeof(arena)));
    C(!kheap_check(&h,(struct heap_stats *)&h));C(!kheap_check(&h,(struct heap_stats *)arena));
    C(!kheap_alloc(&h,0));C(!kheap_alloc(&h,SIZE_MAX));C(!kheap_calloc(&h,SIZE_MAX,2));C(!kheap_free(&h,arena+1));
    unsigned char *p=kheap_calloc(&h,17,3);C(p==arena);for(unsigned i=0;i<51;i++)C(p[i]==0);memset(p,0x55,51);
    unsigned char *q=kheap_realloc(&h,p,100);C(q==arena+64);for(unsigned i=0;i<51;i++)C(q[i]==0x55);C(!kheap_free(&h,p));C(kheap_free(&h,q));
    p=kheap_alloc(&h,sizeof(arena));C(p==arena);C(!kheap_alloc(&h,1));C(!kheap_realloc(&h,p,SIZE_MAX));C(kheap_free(&h,p));C(!kheap_free(&h,p));
    for(unsigned step=0;step<20000;step++){
        unsigned i=rnd()%64;
        if(slots[i].p){p=slots[i].p;size_t n=slots[i].n;for(size_t j=0;j<n;j++)C(p[j]==slots[i].tag);
            C(kheap_free(&h,p));mark(p,n,1,0);slots[i].p=NULL;
        }else{
            size_t n=1+rnd()%2048,need=(n+15)/16;size_t expected=sizeof(used);
            for(size_t j=0;j+need<=sizeof(used);j++){size_t k=0;while(k<need&&!used[j+k])k++;if(k==need){expected=j;break;}j+=k;}
            p=kheap_alloc(&h,n);C((p!=NULL)==(expected!=sizeof(used)));
            if(p){C(p==arena+expected*16);mark(p,n,0,1);slots[i].p=p;slots[i].n=n;slots[i].tag=(unsigned char)(i^step);memset(p,slots[i].tag,n);}
        }
        struct heap_stats s;C(kheap_check(&h,&s));size_t n=0;for(size_t j=0;j<sizeof(used);j++)n+=used[j];C(s.used==n*16&&s.free+s.used==sizeof(arena));
    }
    for(unsigned i=0;i<64;i++)if(slots[i].p){C(kheap_free(&h,slots[i].p));mark(slots[i].p,slots[i].n,1,0);}
    struct heap_stats s;C(kheap_check(&h,&s)&&s.live==0&&s.extents==1&&s.largest==sizeof(arena));
    /* Exhaust metadata, then coalesce and recover. */
    void *a[KHEAP_EXTENTS];for(unsigned i=0;i<KHEAP_EXTENTS-1;i++){a[i]=kheap_alloc(&h,16);C(a[i]!=NULL);}C(!kheap_alloc(&h,16));
    for(unsigned i=0;i<KHEAP_EXTENTS-1;i++){C(kheap_free(&h,a[i]));}C(kheap_check(&h,&s)&&s.extents==1);
    printf("M13 heap assertions: %lu PASS; 20000 randomized operations\n",checks);
}
static void device_test(void){unsigned long start=checks;uint32_t a;
    for(unsigned b=0;b<256;b++)for(unsigned d=0;d<32;d++)for(unsigned f=0;f<8;f++){C(pci_config_address(b,d,f,252,&a));C(a==(0x800000fcu|(b<<16)|(d<<11)|(f<<8)));}
    C(!pci_config_address(256,0,0,0,&a));C(!pci_config_address(0,32,0,0,&a));C(!pci_config_address(0,0,8,0,&a));C(!pci_config_address(0,0,0,1,&a));C(!pci_config_address(0,0,0,256,&a));C(!pci_config_address(0,0,0,0,NULL));
    struct key_decoder k={0};key_decode(&k,0xe0);key_decode(&k,0x1e);C(!k.press);key_decode(&k,0x1e);key_decode(&k,0x1e);C(k.press==1&&k.down);key_decode(&k,0x9e);C(k.release==1&&!k.down);key_decode(&k,0x9e);C(k.invalid==1);
    printf("M14 device assertions: %lu PASS\n",checks-start);
}
static void scheduler_test(void){unsigned long start=checks;
    for(unsigned n=0;n<256;n++){unsigned s[SCHED_TASKS];for(unsigned j=0;j<SCHED_TASKS;j++)s[j]=(n>>(2*j))&3;
        for(unsigned i=0;i<SCHED_TASKS;i++){unsigned e=SCHED_TASKS;for(unsigned k=1;k<=SCHED_TASKS;k++)if(s[(i+k)%SCHED_TASKS]==SCHED_READY){e=(i+k)%SCHED_TASKS;break;}C(sched_pick(s,i)==e);}}
    struct sched_frame f;C(sched_frame_init(&f,0xffffffff80001000ULL,0xffff800000001ff8ULL,3));C(f.rdi==3&&f.cs==8&&f.ss==16&&f.rflags==0x202&&f.rsp==0xffff800000001ff8ULL&&f.rip==0xffffffff80001000ULL&&f.rax==0);
    C(!sched_frame_init(&f,0x800000000000ULL,0xffff800000001ff8ULL,3));C(!sched_frame_init(&f,1,16,0));C(!sched_frame_init(NULL,1,8,0));C(sched_pick(NULL,0)==SCHED_TASKS);
    printf("M15 scheduler assertions: %lu PASS\n",checks-start);
}
int main(void){heap_test();device_test();scheduler_test();return 0;}
