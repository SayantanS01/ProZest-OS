/* Included by the RC3 entry after the RC2 PMM boot smoke test. */
#ifndef PROZEST_VMM_FAULT
#define PROZEST_VMM_FAULT 0
#endif
static struct vmm_space vmm_original, vmm_active;
static uint64_t vmm_old_cr3;
static volatile uint64_t *vmm_resolve(void *ctx,uint64_t pa)
{
    (void)ctx;
    if (!pa || (pa&4095) || pa>UINT64_MAX-inventory.hhdm ||
        !vmm_canonical(inventory.hhdm+pa)) return NULL;
    int known=pmm_is_managed_address(pa);
    for (uint32_t i=0;!known&&i<tables.count;i++) known=tables.frame[i].physical==pa;
    if (!known) return NULL;
    return (volatile uint64_t *)(uintptr_t)(inventory.hhdm+pa);
}
static enum vmm_status vmm_take(void *ctx,uint64_t *pa)
{
    (void)ctx;
    enum pmm_status r=pmm_alloc_page(pa);
    if (r!=PMM_OK) return r==PMM_OUT_OF_MEMORY?VMM_OOM:VMM_BROKEN;
    struct vmm_translation t;
    if (!vmm_resolve(NULL,*pa)||vmm_translate(&vmm_original,inventory.hhdm+*pa,&t)!=VMM_OK ||
        t.physical!=*pa || !(t.flags&VMM_W) || (t.flags&VMM_U)) {
        (void)pmm_free_page(*pa); return VMM_BROKEN;
    }
    return VMM_OK;
}
static int vmm_give(void *ctx,uint64_t pa) { (void)ctx; return pmm_free_page(pa)==PMM_OK; }
static void vmm_flush(void *ctx,uint64_t va)
{
    (void)ctx; __asm__ volatile("invlpg (%0)"::"r"(va):"memory");
}
static uint64_t vmm_cr3(void) { uint64_t x; __asm__ volatile("mov %%cr3,%0":"=r"(x)); return x; }
static void vmm_switch(uint64_t root)
{
    uint64_t value=root|(vmm_old_cr3&UINT64_C(0x18));
    __asm__ volatile("mov %0,%%cr3"::"r"(value):"memory");
    check("VMM CR3 SWITCH: PASS",vmm_cr3()==value);
}
static int vmm_same(uint64_t va,const struct vmm_space *s)
{
    struct vmm_translation a,b;
    return vmm_translate(&vmm_original,va,&a)==VMM_OK && vmm_translate(s,va,&b)==VMM_OK &&
           a.physical==b.physical && a.page_size==b.page_size && a.flags==b.flags;
}
static int vmm_counts(const struct vmm_space *s)
{
    struct pmm_stats st;
    return pmm_get_stats(&st)==PMM_OK && st.allocated_pages==s->mapped+s->table_pages &&
           st.free_pages+st.allocated_pages==st.managed_pages && pmm_live_state_check();
}
static void vmm_clone_checked(const struct vmm_backend *io,unsigned bits,unsigned huge)
{
    check("VMM ROOT CLONE: PASS",vmm_clone(&vmm_active,io,vmm_original.root,bits,1,huge)==VMM_OK);
    uint64_t rsp; __asm__ volatile("mov %%rsp,%0":"=r"(rsp));
    check("VMM INHERITED MAPPINGS: PASS",vmm_same((uint64_t)(uintptr_t)vmm_switch,&vmm_active)&&
          vmm_same(rsp-1,&vmm_active)&&vmm_same((uint64_t)(uintptr_t)&vmm_active,&vmm_active)&&
          vmm_same(inventory.hhdm+allocator_plan.metadata_base,&vmm_active));
    struct vmm_translation t;
    check("VMM PRIVATE SLOT EMPTY: PASS",vmm_translate(&vmm_original,vmm_active.window,&t)==VMM_ABSENT &&
          vmm_translate(&vmm_active,vmm_active.window,&t)==VMM_ABSENT && vmm_counts(&vmm_active));
    vmm_switch(vmm_active.root);
}
static void vmm_run(void)
{
    unsigned a=0,b=0,c=0,d=0; uint64_t cr0,cr4;
    check("VMM CPUID WIDTH: PASS",__get_cpuid(0x80000008,&a,&b,&c,&d));
    unsigned bits=a&255;
    check("VMM NX SUPPORT: PASS",__get_cpuid(0x80000001,&a,&b,&c,&d)&&(d&(1u<<20)));
    unsigned huge=(d>>26)&1;
    __asm__ volatile("mov %%cr0,%0":"=r"(cr0));
    __asm__ volatile("mov %%cr4,%0":"=r"(cr4));
    vmm_old_cr3=vmm_cr3();
    uint64_t efer=read_msr(0xc0000080);
    check("M11.1 PAGING DIAGNOSTICS: PASS",bits>=32&&bits<=52&&(cr0&(UINT64_C(1)<<31))&&
          (cr4&32)&&!(cr4&((UINT64_C(1)<<12)|(UINT64_C(1)<<17)|(UINT64_C(1)<<22)|(UINT64_C(1)<<24)))&&
          (efer&(UINT64_C(1)<<10))&&!(vmm_old_cr3&~(VMM_ADDR|UINT64_C(0x18)))&&
          (vmm_old_cr3&VMM_ADDR)==tables.root);
    /* Require NXE before interpreting NX; enable it if firmware left it off.
       WP makes supervisor writes obey read-only PTEs. */
    if (!(efer&(UINT64_C(1)<<11))) {
        efer|=UINT64_C(1)<<11;
        __asm__ volatile("wrmsr"::"c"(0xc0000080u),"a"((uint32_t)efer),"d"((uint32_t)(efer>>32)):"memory");
    }
    if (!(cr0&(UINT64_C(1)<<16))) { cr0|=UINT64_C(1)<<16; __asm__ volatile("mov %0,%%cr0"::"r"(cr0):"memory"); }
    check("M11.2 PTE FORMAT: PASS",vmm_canonical(UINT64_C(0xffff800000000000))&&
          !vmm_canonical(UINT64_C(0x0000800000000000))&&vmm_index(UINT64_C(0xffff800000000000),4)==256&&
          (read_msr(0xc0000080)&(UINT64_C(1)<<11)));
    number("VMM CR0: ",cr0); number("VMM CR4: ",cr4);
    number("VMM EFER: ",read_msr(0xc0000080)); number("VMM PHYSICAL BITS: ",bits);
    struct vmm_backend io={NULL,vmm_resolve,vmm_take,vmm_give,vmm_flush};
    vmm_original=(struct vmm_space){.io=io,.root=vmm_old_cr3&VMM_ADDR,.bits=bits,.nx=1,.huge1g=huge};
    struct vmm_translation t;
    uint64_t code=(uint64_t)(uintptr_t)vmm_run;
    check("M11.3 TRANSLATION: PASS",vmm_translate(&vmm_original,code,&t)==VMM_OK&&
          t.physical==image_physical+code-executable_request.response->virtual_base&&!(t.flags&VMM_U));
    number("VMM ORIGINAL ROOT: ",vmm_original.root);
    vmm_clone_checked(&io,bits,huge);
    uint64_t va=vmm_active.window,pa=0,other=0;
    check("M11.4 TABLE ALLOCATION: PASS",vmm_active.table_pages==1&&vmm_counts(&vmm_active));
    check("M11.5 PAGE MAP: PASS",vmm_map_page(&vmm_active,va,VMM_READ_WRITE,&pa)==VMM_OK&&
          vmm_active.table_pages==4&&vmm_counts(&vmm_active)&&vmm_translate(&vmm_active,va+123,&t)==VMM_OK&&
          t.physical==pa+123&&t.page_size==4096&&t.flags==(VMM_P|VMM_W|VMM_NX));
    volatile uint64_t *ptr=(volatile uint64_t *)(uintptr_t)va;
    check("VMM ZEROED PAYLOAD: PASS",*ptr==0);
    *ptr=UINT64_C(0x123456789abcdef0);
    check("VMM HARDWARE ALIAS: PASS",*vmm_resolve(NULL,pa)==*ptr);
    check("VMM MAP REJECTION: PASS",vmm_map_page(&vmm_active,va,VMM_READ_WRITE,&other)==VMM_EXISTS&&
          !other&&vmm_map_page(&vmm_active,va+1,VMM_READ_WRITE,&other)==VMM_INVALID&&
          vmm_map_page(&vmm_active,va+4096,VMM_W,&other)==VMM_INVALID&&vmm_counts(&vmm_active));
    check("M11.6 UNMAP AND TLB: PASS",vmm_unmap_page(&vmm_active,va)==VMM_OK&&
          vmm_translate(&vmm_active,va,&t)==VMM_ABSENT&&vmm_active.table_pages==1&&vmm_counts(&vmm_active)&&
          vmm_unmap_page(&vmm_active,va)==VMM_ABSENT);
    check("VMM REMAP: PASS",vmm_map_page(&vmm_active,va,VMM_READ_WRITE,&pa)==VMM_OK&&*ptr==0);
    *ptr=UINT64_C(0xabcdef);
    check("M11.7 PERMISSIONS: PASS",vmm_protect_page(&vmm_active,va,VMM_READ_ONLY)==VMM_OK&&
          vmm_translate(&vmm_active,va,&t)==VMM_OK&&t.flags==(VMM_P|VMM_NX)&&*ptr==UINT64_C(0xabcdef)&&
          vmm_protect_page(&vmm_active,va,VMM_READ_WRITE)==VMM_OK);
    /* mov eax,0x12345678; ret -- write under RW/NX, execute under RO/X. */
    volatile uint8_t *codebytes=(volatile uint8_t *)(uintptr_t)va;
    const uint8_t program[]={0xb8,0x78,0x56,0x34,0x12,0xc3};
    for(unsigned j=0;j<sizeof(program);j++) codebytes[j]=program[j];
    check("VMM EXECUTABLE MAP: PASS",vmm_protect_page(&vmm_active,va,VMM_EXECUTE)==VMM_OK&&
          vmm_translate(&vmm_active,va,&t)==VMM_OK&&t.flags==VMM_P);
    uint32_t result;
    __asm__ volatile("call *%1":"=a"(result):"r"(va):"memory");
    check("VMM HARDWARE EXECUTE: PASS",result==0x12345678u&&vmm_unmap_page(&vmm_active,va)==VMM_OK);
    check("M11.8 PRIVATE ADDRESS SPACE: PASS",vmm_cr3()!=(vmm_old_cr3)&&
          vmm_translate(&vmm_original,va,&t)==VMM_ABSENT&&vmm_counts(&vmm_active));
    /* 512 deterministic operations, spread across PT and PD boundaries. */
    uint64_t frames[32]={0}; uint32_t rng=0x9e3779b9u;
    for (unsigned i=0;i<512;i++) {
        rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
        unsigned j=rng&31;
        uint64_t v=va+(uint64_t)j*UINT64_C(0x40201000);
        volatile uint64_t *p=(volatile uint64_t *)(uintptr_t)v;
        if (frames[j]) {
            check("VMM STRESS READ",*p==(UINT64_C(0x51a00000)+j)&&vmm_translate(&vmm_active,v,&t)==VMM_OK&&t.physical==frames[j]);
            check("VMM STRESS FREE",vmm_unmap_page(&vmm_active,v)==VMM_OK); frames[j]=0;
        } else {
            check("VMM STRESS MAP",vmm_map_page(&vmm_active,v,VMM_READ_WRITE,&frames[j])==VMM_OK&&*p==0);
            *p=UINT64_C(0x51a00000)+j;
            for(unsigned k=0;k<32;k++) if(k!=j&&frames[k]) check("VMM STRESS UNIQUE",frames[k]!=frames[j]);
        }
        if (!(i&31)) check("VMM STRESS COUNTERS",vmm_counts(&vmm_active));
    }
    for(unsigned j=0;j<32;j++) if(frames[j]) check("VMM STRESS CLEANUP",vmm_unmap_page(&vmm_active,va+(uint64_t)j*UINT64_C(0x40201000))==VMM_OK);
    check("M11.9 STRESS CLEANUP: PASS",vmm_active.mapped==0&&vmm_active.table_pages==1&&vmm_counts(&vmm_active));
    vmm_switch(vmm_original.root);
    check("VMM ROOT RELEASE: PASS",vmm_destroy_empty(&vmm_active)==VMM_OK);
    struct pmm_stats st;
    check("VMM ZERO LEAKS: PASS",pmm_get_stats(&st)==PMM_OK&&!st.allocated_pages&&st.free_pages==st.managed_pages&&pmm_live_state_check());
    vmm_clone_checked(&io,bits,huge);
    number("VMM ACTIVE ROOT: ",vmm_active.root);
    number("VMM PRIVATE WINDOW: ",vmm_active.window);
    check("VMM FINAL ROOT ONLY: PASS",vmm_counts(&vmm_active)&&vmm_active.table_pages==1&&!vmm_active.mapped);
    number("VMM FINAL ALLOCATED: ",1);
#if PROZEST_VMM_FAULT != 0
    va=vmm_active.window;
#if PROZEST_VMM_FAULT == 1
    check("VMM FAULT MAP: PASS",vmm_map_page(&vmm_active,va,VMM_READ_WRITE,&pa)==VMM_OK);
    *(volatile uint64_t *)(uintptr_t)va=7;
    check("VMM FAULT PRECONDITION: PASS",*(volatile uint64_t *)(uintptr_t)va==7&&
          vmm_unmap_page(&vmm_active,va)==VMM_OK&&vmm_translate(&vmm_active,va,&t)==VMM_ABSENT);
#elif PROZEST_VMM_FAULT == 2
    check("VMM FAULT MAP: PASS",vmm_map_page(&vmm_active,va,VMM_READ_WRITE,&pa)==VMM_OK);
    *(volatile uint64_t *)(uintptr_t)va=7;
    check("VMM FAULT PRECONDITION: PASS",vmm_protect_page(&vmm_active,va,VMM_READ_ONLY)==VMM_OK);
#elif PROZEST_VMM_FAULT == 3
    check("VMM FAULT MAP: PASS",vmm_map_page(&vmm_active,va,VMM_READ_WRITE,&pa)==VMM_OK);
    *(volatile uint8_t *)(uintptr_t)va=0xc3; /* RET, if NX were broken */
    check("VMM FAULT RX: PASS",vmm_protect_page(&vmm_active,va,VMM_EXECUTE)==VMM_OK);
    __asm__ volatile("call *%0"::"r"(va):"memory");
    check("VMM FAULT PRECONDITION: PASS",vmm_protect_page(&vmm_active,va,VMM_READ_WRITE)==VMM_OK);
#else
#error Invalid fault mode
#endif
    number("VMM EXPECTED FAULT VA: ",va);
    number("VMM FAULT MODE: ",PROZEST_VMM_FAULT);
    qemu_debugcon_puts("VMM INTENTIONAL FAULT\n");
#if PROZEST_VMM_FAULT == 1
    __asm__ volatile("mov (%0),%%rax"::"r"(va):"rax","memory");
#elif PROZEST_VMM_FAULT == 2
    __asm__ volatile("movq $1,(%0)"::"r"(va):"memory");
#else
    __asm__ volatile("call *%0"::"r"(va):"memory");
#endif
    check("VMM FAULT RETURNED",0);
#endif
    log_write(LOG_OK,"Virtual memory manager active");
    log_write(LOG_OK,"Kernel-controlled PML4 active");
    qemu_debugcon_puts("M11.10 VMM INTEGRATION: PASS\n");
}
