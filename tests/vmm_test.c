#include "vmm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 256u
static uint64_t mem[N][512];
static unsigned char used[N];
static unsigned budget, live, flushes, checks;
#define C(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static volatile uint64_t *resolve(void *ctx,uint64_t pa) {
    (void)ctx; return !(pa&4095)&&pa/4096<N&&used[pa/4096]?mem[pa/4096]:NULL;
}
static enum vmm_status take(void *ctx,uint64_t *pa) {
    (void)ctx; if (!budget) return VMM_OOM;
    for(unsigned i=5;i<N;i++) if(!used[i]) { used[i]=1; live++; budget--; *pa=(uint64_t)i*4096; memset(mem[i],0xa5,sizeof mem[i]); return VMM_OK; }
    return VMM_OOM;
}
static int give(void *ctx,uint64_t pa) {
    (void)ctx; unsigned i=(unsigned)(pa/4096); C(i>=5&&i<N&&used[i]&&!(pa&4095)); used[i]=0; live--; return 1;
}
static void flush(void *ctx,uint64_t va) { (void)ctx; C(vmm_canonical(va)&&!(va&4095)); flushes++; }
static const struct vmm_backend io={NULL,resolve,take,give,flush};
static uint64_t digest(void) { uint64_t h=0xcbf29ce484222325ULL; for(unsigned i=0;i<N;i++) if(used[i]) { h=(h^i)*1099511628211ULL; for(unsigned j=0;j<512;j++) h=(h^mem[i][j])*1099511628211ULL; } return h; }
int main(void) {
    used[1]=used[2]=used[3]=used[4]=1; budget=1000000;
    /* Original lower-half tree: 1 GiB, 2 MiB, and 4 KiB leaves. */
    mem[1][0]=8192|7; mem[2][0]=0x40000000ULL|0x87; mem[2][1]=12288|7;
    mem[3][0]=0x200000ULL|0x83|VMM_NX; mem[3][1]=16384|7; mem[4][0]=0x9000|VMM_P|VMM_U;
    struct vmm_space old={.io=io,.root=4096,.bits=48,.nx=1,.huge1g=1},s={0};
    struct vmm_translation t={0};
    C(vmm_canonical(0)&&vmm_canonical(0x7fffffffffffULL)&&vmm_canonical(0xffff800000000000ULL)&&vmm_canonical(UINT64_MAX));
    C(!vmm_canonical(0x800000000000ULL)&&!vmm_canonical(0xffff7fffffffffffULL));
    C(vmm_index(UINT64_MAX,4)==511&&vmm_index(UINT64_MAX,1)==511&&vmm_index(0,0)==512&&vmm_index(0,5)==512);
    C(vmm_translate(&old,123,&t)==VMM_OK&&t.physical==0x40000000+123&&t.page_size==(1ULL<<30)&&t.flags==7);
    C(vmm_translate(&old,0x40000000+321,&t)==VMM_OK&&t.physical==0x200000+321&&t.page_size==(1ULL<<21)&&t.flags==(VMM_P|VMM_W|VMM_NX));
    C(vmm_translate(&old,0x40200000+123,&t)==VMM_OK&&t.physical==0x9000+123&&t.page_size==4096&&t.flags==(VMM_P|VMM_U));
    C(vmm_translate(&old,0x40201000,&t)==VMM_ABSENT);
    C(vmm_translate(&old,0x800000000000ULL,&t)==VMM_INVALID);
    mem[2][0]|=0x2000; C(vmm_translate(&old,0,&t)==VMM_BROKEN); mem[2][0]&=~0x2000ULL;
    old.huge1g=0; C(vmm_translate(&old,0,&t)==VMM_BROKEN); old.huge1g=1;
    old.nx=0; C(vmm_translate(&old,0x40000000,&t)==VMM_BROKEN); old.nx=1;
    uint64_t baseline=digest(); budget=0; C(vmm_clone(&s,&io,old.root,48,1,1)==VMM_OOM&&!s.root&&digest()==baseline);
    budget=1000000; C(vmm_clone(&s,&io,old.root,48,1,1)==VMM_OK&&live==1&&s.table_pages==1);
    uint64_t va=s.window,pa=0; C(vmm_translate(&old,va,&t)==VMM_ABSENT);
    uint64_t cloned=digest();
    for(unsigned i=0;i<4;i++) { budget=i; pa=0xdead; unsigned f=flushes;
        C(vmm_map_page(&s,va,VMM_READ_WRITE,&pa)==VMM_OOM&&pa==0xdead&&live==1&&s.mapped==0&&s.table_pages==1&&digest()==cloned&&flushes==f);
    }
    budget=1000000; C(vmm_map_page(&s,va,VMM_READ_WRITE,&pa)==VMM_OK&&live==5&&s.table_pages==4&&s.mapped==1);
    for(unsigned j=0;j<512;j++) C(resolve(NULL,pa)[j]==0);
    C(vmm_translate(&s,va+4095,&t)==VMM_OK&&t.physical==pa+4095&&t.flags==(VMM_P|VMM_W|VMM_NX));
    uint64_t d=digest(),sentinel=0xfeed; C(vmm_map_page(&s,va,VMM_READ_ONLY,&sentinel)==VMM_EXISTS&&sentinel==0xfeed&&digest()==d);
    C(vmm_map_page(&s,va+1,VMM_READ_ONLY,&sentinel)==VMM_INVALID&&vmm_map_page(&s,0,VMM_READ_ONLY,&sentinel)==VMM_INVALID&&vmm_map_page(&s,va+4096,VMM_W,&sentinel)==VMM_INVALID&&digest()==d);
    C(vmm_protect_page(&s,va,VMM_READ_ONLY)==VMM_OK&&vmm_translate(&s,va,&t)==VMM_OK&&t.flags==(VMM_P|VMM_NX));
    C(vmm_protect_page(&s,va,VMM_EXECUTE)==VMM_OK&&vmm_translate(&s,va,&t)==VMM_OK&&t.flags==VMM_P);
    C(vmm_destroy_empty(&s)==VMM_INVALID&&vmm_unmap_page(&s,va)==VMM_OK&&live==1&&s.table_pages==1&&digest()==cloned);
    C(vmm_unmap_page(&s,va)==VMM_ABSENT);
    uint64_t adjacent[4]={0};
    uint64_t offsets[]={0,4096,0x200000,0x40000000};
    for(unsigned j=0;j<4;j++) C(vmm_map_page(&s,va+offsets[j],VMM_READ_WRITE,&adjacent[j])==VMM_OK);
    C(s.table_pages==7&&s.mapped==4);
    C(vmm_unmap_page(&s,va)==VMM_OK&&s.table_pages==7);
    C(vmm_translate(&s,va+4096,&t)==VMM_OK&&t.physical==adjacent[1]);
    C(vmm_unmap_page(&s,va+4096)==VMM_OK&&s.table_pages==6);
    C(vmm_unmap_page(&s,va+0x200000)==VMM_OK&&s.table_pages==4);
    C(vmm_unmap_page(&s,va+0x40000000)==VMM_OK&&s.table_pages==1&&digest()==cloned);
    /* Randomized mixed map/unmap/protect with independent per-slot model. */
    uint64_t frames[48]={0},flags[48]={0}; uint32_t rng=0x87a4b321;
    for(unsigned i=0;i<12000;i++) {
        rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
        unsigned j=rng%48; uint64_t v=va+(uint64_t)j*0x40201000ULL;
        if(!frames[j]) { flags[j]=VMM_READ_WRITE; C(vmm_map_page(&s,v,flags[j],&frames[j])==VMM_OK); C(resolve(NULL,frames[j])[0]==0); resolve(NULL,frames[j])[0]=j+1; }
        else if(rng&0x100) { C(vmm_unmap_page(&s,v)==VMM_OK); frames[j]=0; }
        else { flags[j]=(rng&0x200)?VMM_READ_ONLY:VMM_EXECUTE; C(vmm_protect_page(&s,v,flags[j])==VMM_OK); }
        unsigned count=0;
        for(unsigned k=0;k<48;k++) { uint64_t x=va+(uint64_t)k*0x40201000ULL;
            if(frames[k]) { count++; C(vmm_translate(&s,x+17,&t)==VMM_OK&&t.physical==frames[k]+17&&t.flags==(VMM_P|flags[k])&&resolve(NULL,frames[k])[0]==k+1); for(unsigned l=0;l<k;l++) C(frames[l]!=frames[k]); }
            else C(vmm_translate(&s,x,&t)==VMM_ABSENT);
        }
        C(count==s.mapped&&live==s.mapped+s.table_pages);
    }
    for(unsigned j=0;j<48;j++) if(frames[j]) C(vmm_unmap_page(&s,va+(uint64_t)j*0x40201000ULL)==VMM_OK);
    C(live==1&&s.table_pages==1&&s.mapped==0&&digest()==cloned);
    C(vmm_destroy_empty(&s)==VMM_OK&&live==0&&!s.root&&digest()==baseline);
    printf("M11 host assertions: %u/%u PASS; 12000 randomized iterations; zero leaked frames\n",checks,checks);
    return 0;
}
