/* BSP only, IF=0. Fixed PMM-owned contiguous arena, accessed through the
 * inherited HHDM. No heap calls from IRQ handlers. */
#include "kheap.h"
#define HEAP_PAGES 64u
#define HEAP_BYTES (HEAP_PAGES*4096u)
static struct kheap kernel_heap;
static uint64_t heap_pa;
void *kmalloc(size_t n){return (irq_flags()&0x200)?NULL:kheap_alloc(&kernel_heap,n);}
int kfree(void *p){return !(irq_flags()&0x200)&&kheap_free(&kernel_heap,p);}
void *kcalloc(size_t n,size_t s){return (irq_flags()&0x200)?NULL:kheap_calloc(&kernel_heap,n,s);}
void *krealloc(void *p,size_t n){return (irq_flags()&0x200)?NULL:kheap_realloc(&kernel_heap,p,n);}
static void heap_open(void){
    check("HEAP ARENA ALLOCATION",!kernel_heap.base&&pmm_alloc_contiguous(HEAP_PAGES,1,&heap_pa)==PMM_OK);
    check("HEAP HHDM RANGE",inventory.hhdm<=UINT64_MAX-HEAP_BYTES&&heap_pa<=UINT64_MAX-inventory.hhdm-HEAP_BYTES);
    for(unsigned i=0;i<HEAP_PAGES;i++){
        uint64_t pa=heap_pa+i*4096;struct vmm_translation t;
        check("HEAP PAGE MAPPING",vmm_translate(&vmm_active,inventory.hhdm+pa,&t)==VMM_OK&&t.physical==pa&&(t.flags&VMM_W)&&!(t.flags&VMM_U));
    }
    unsigned char *p=(unsigned char *)(uintptr_t)(inventory.hhdm+heap_pa);
    for(size_t i=0;i<HEAP_BYTES;i++)p[i]=0;
    check("HEAP ARENA INITIALIZED",kheap_init(&kernel_heap,p,HEAP_BYTES));
}
static void heap_close(void){
    struct heap_stats s;
    check("HEAP ARENA EMPTY",kheap_check(&kernel_heap,&s)&&!s.live&&s.free==HEAP_BYTES&&s.largest==HEAP_BYTES&&s.extents==1);
    for(size_t i=0;i<HEAP_BYTES;i++)kernel_heap.base[i]=0;
    check("HEAP ARENA RELEASE",pmm_free_contiguous(heap_pa,HEAP_PAGES)==PMM_OK);
    kernel_heap=(struct kheap){0};heap_pa=0;
    check("HEAP PMM ROOT ONLY",vmm_counts(&vmm_active)&&pmm_live_state_check());
}
static uint32_t heap_rng(uint32_t *s){uint32_t x=*s;x^=x<<13;x^=x>>17;x^=x<<5;return *s=x;}
static struct {unsigned char *p;size_t n;unsigned char tag;} heap_slots[64];
static void heap_run(void){
    check("M13.1 HEAP PREINIT: PASS",!kmalloc(1)&&!kfree((void *)16));
    heap_open();number("HEAP ARENA PA: ",heap_pa);number("HEAP ARENA BYTES: ",HEAP_BYTES);
    check("M13.2 HEAP BACKING: PASS",kernel_heap.bytes==HEAP_BYTES);
    unsigned char *a=kmalloc(17),*b=kmalloc(32),*c=kmalloc(48);
    check("M13.3 HEAP ALIGNMENT: PASS",a&&b&&c&&!( (uintptr_t)a&15)&&b==a+32&&c==b+32);
    check("M13.4 HEAP FREE: PASS",kfree(b)&&!kfree(b)&&!kfree(a+1)&&kfree(NULL));
    check("M13.5 HEAP REUSE: PASS",kmalloc(32)==b&&kfree(b)&&kfree(a)&&kfree(c));
    struct heap_stats s;check("M13.6 HEAP COALESCE: PASS",kheap_check(&kernel_heap,&s)&&s.largest==HEAP_BYTES&&s.extents==1);
    a=kcalloc(31,3);check("HEAP CALLOC POINTER",a!=NULL);
    for(unsigned i=0;i<93;i++)check("HEAP CALLOC ZERO",a[i]==0);
    for(unsigned i=0;i<93;i++)a[i]=(unsigned char)i;
    b=krealloc(a,257);check("HEAP REALLOC POINTER",b!=NULL);
    for(unsigned i=0;i<93;i++)check("HEAP REALLOC COPY",b[i]==(unsigned char)i);
    check("M13.7 HEAP CALLOC REALLOC: PASS",!kcalloc(SIZE_MAX,2)&&!krealloc(b,SIZE_MAX)&&kfree(b));
    a=kmalloc(HEAP_BYTES);check("M13.8 HEAP OOM: PASS",a&&!kmalloc(1)&&!kmalloc(0)&&kfree(a));
    uint32_t rng=0x6a09e667;unsigned allocs=0,frees=0;size_t peak=0;
    for(unsigned step=0;step<4096;step++){
        unsigned i=heap_rng(&rng)%64;
        if(heap_slots[i].p){
            for(size_t j=0;j<heap_slots[i].n;j++)if(heap_slots[i].p[j]!=heap_slots[i].tag)check("HEAP PAYLOAD CORRUPTION",0);
            if(!kfree(heap_slots[i].p))check("HEAP STRESS FREE",0);
            heap_slots[i].p=NULL;frees++;
        }else{
            size_t n=1+heap_rng(&rng)%2048;unsigned char *p=kmalloc(n);if(!p)check("HEAP STRESS ALLOCATION",0);
            unsigned char tag=(unsigned char)(i^step);for(size_t j=0;j<n;j++)p[j]=tag;
            heap_slots[i].p=p;heap_slots[i].n=n;heap_slots[i].tag=tag;allocs++;
        }
        if(!kheap_check(&kernel_heap,&s))check("HEAP STRESS INVARIANT",0);
        if(s.used>peak)peak=s.used;
    }
    for(unsigned i=0;i<64;i++)if(heap_slots[i].p){
        for(size_t j=0;j<heap_slots[i].n;j++)if(heap_slots[i].p[j]!=heap_slots[i].tag)check("HEAP FINAL PAYLOAD",0);
        if(!kfree(heap_slots[i].p))check("HEAP FINAL FREE",0);
        heap_slots[i].p=NULL;frees++;
    }
    number("HEAP STRESS STEPS: ",4096);number("HEAP STRESS ALLOCS: ",allocs);number("HEAP STRESS FREES: ",frees);number("HEAP PEAK BYTES: ",peak);
    check("M13.9 HEAP STRESS: PASS",allocs==frees&&kheap_check(&kernel_heap,&s)&&s.live==0&&s.free==HEAP_BYTES);
    heap_close();check("M13.10 HEAP INTEGRATION: PASS",!kernel_heap.base);
    log_write(LOG_OK,"Kernel heap allocation and reuse verified");qemu_debugcon_puts("M13 COMPLETE\n");
}
