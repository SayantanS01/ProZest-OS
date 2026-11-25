/* BSP-only interrupt bring-up. No IRQ handler allocates, logs, or enables IF. */
#include "irq_platform.h"
extern void irq_pic0(void),irq_pic7(void),irq_pic15(void),irq_io0(void),irq_lapic_timer(void),irq_soft(void),irq_spurious(void);
extern int irq_register_probe(void);
struct irq_gate { uint16_t low,selector; uint8_t ist,attr; uint16_t mid; uint32_t high,zero; } __attribute__((packed));
struct irq_idtr { uint16_t limit; uint64_t base; } __attribute__((packed));
_Static_assert(sizeof(struct irq_gate)==16,"IDT gate");
static struct irq_gate irq_idt[256] __attribute__((aligned(4096)));
static volatile uint64_t irq_pic_ticks,irq_io_ticks,irq_apic_ticks,irq_soft_ticks,irq_spurious_ticks,irq_bad;
static volatile uint32_t *irq_lapic,*irq_ioapic;
static unsigned irq_x2;
#if PROZEST_STAGE >= 15
static uint64_t services_lapic_pa;
static uint32_t services_lapic_period;
#endif
static volatile uint64_t *irq_pt,*irq_root;
static uint64_t irq_frames[3],irq_window;
static inline void irq_out(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p):"memory");}
static inline uint8_t irq_in(uint16_t p){uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p):"memory");return v;}
static void irq_wait_io(void){irq_out(0x80,0);}
static void irq_wrmsr(uint32_t r,uint64_t v){__asm__ volatile("wrmsr"::"c"(r),"a"((uint32_t)v),"d"((uint32_t)(v>>32)):"memory");}
static uint32_t irq_lread(unsigned r){return irq_x2?(uint32_t)read_msr(0x800+r/16):irq_lapic[r/4];}
static void irq_lwrite(unsigned r,uint32_t v){if(irq_x2)irq_wrmsr(0x800+r/16,v);else {irq_lapic[r/4]=v;(void)irq_lapic[0x20/4];}}
static uint32_t irq_ioread(unsigned r){irq_ioapic[0]=r;return irq_ioapic[4];}
static void irq_iowrite(unsigned r,uint32_t v){irq_ioapic[0]=r;irq_ioapic[4]=v;}
static uint64_t irq_flags(void){uint64_t f;__asm__ volatile("pushfq; popq %0":"=r"(f));return f;}
static uint64_t irq_tsc(void){uint32_t a,d;__asm__ volatile("rdtsc":"=a"(a),"=d"(d));return ((uint64_t)d<<32)|a;}
static int irq_isr(unsigned v){return !!(irq_lread(0x100+(v/32)*16)&(1u<<(v%32)));}
void irq_dispatch(uint64_t vector){
    if(irq_flags()&0x600)irq_bad++;
    switch(vector){
    case 32:
        irq_out(0x20,0x0b);if(!(irq_in(0x20)&1))irq_bad++;
        irq_pic_ticks++;irq_out(0x20,0x20);break;
    case 39:
        irq_out(0x20,0x0b);if(irq_in(0x20)&0x80){irq_bad++;irq_out(0x20,0x20);}else irq_spurious_ticks++;break;
    case 47:
        irq_out(0xa0,0x0b);if(irq_in(0xa0)&0x80){irq_bad++;irq_out(0xa0,0x20);}else irq_spurious_ticks++;
        irq_out(0x20,0x20);break;
    case 64: if(!irq_isr(64))irq_bad++;irq_io_ticks++;irq_lwrite(0xb0,0);break;
    case 65: if(!irq_isr(65))irq_bad++;irq_apic_ticks++;irq_lwrite(0xb0,0);break;
    case 66: irq_soft_ticks++;break;
    case 255: irq_spurious_ticks++;break; /* APIC spurious interrupt has no EOI. */
    default: irq_bad++;break;
    }
}
static void irq_gate_set(unsigned v,void (*entry)(void)){
    uint64_t p=(uint64_t)(uintptr_t)entry;
    irq_idt[v]=(struct irq_gate){(uint16_t)p,8,0,0x8e,(uint16_t)(p>>16),(uint32_t)(p>>32),0};
}
static void irq_install(void){
    struct irq_idtr old,now;__asm__ volatile("sidt %0":"=m"(old));
    check("IRQ OLD IDT: PASS",old.limit==4095&&!(irq_flags()&0x200));
    const volatile struct irq_gate *g=(const volatile struct irq_gate *)(uintptr_t)old.base;
    for(unsigned i=0;i<256;i++)irq_idt[i]=g[i];
    irq_gate_set(32,irq_pic0);irq_gate_set(39,irq_pic7);irq_gate_set(47,irq_pic15);
    irq_gate_set(64,irq_io0);irq_gate_set(65,irq_lapic_timer);irq_gate_set(66,irq_soft);irq_gate_set(255,irq_spurious);
    for(unsigned i=0;i<32;i++){
        const volatile uint8_t *a=(const volatile uint8_t *)&g[i];const uint8_t *b=(const uint8_t *)&irq_idt[i];
        for(unsigned j=0;j<16;j++)check("IRQ EXCEPTION GATE PRESERVED",a[j]==b[j]);
    }
    struct irq_idtr id={4095,(uint64_t)(uintptr_t)irq_idt};
    __asm__ volatile("lidt %0"::"m"(id):"memory");__asm__ volatile("sidt %0":"=m"(now));
    check("M12.2 IRQ IDT: PASS",now.base==id.base&&now.limit==id.limit);
    number("IRQ IDT BASE: ",now.base);
    struct vmm_translation a,b;
    check("IRQ IDT TRANSLATION: PASS",vmm_translate(&vmm_active,old.base,&a)==VMM_OK&&vmm_translate(&vmm_active,now.base,&b)==VMM_OK&&!(b.flags&VMM_U));
    number("IRQ OLD IDT PA: ",a.physical);number("IRQ NEW IDT PA: ",b.physical);
    check("M12.3 IRQ DISPATCH: PASS",irq_register_probe()&&irq_soft_ticks==1&&!irq_bad);
    for(unsigned i=0;i<1023;i++)check("IRQ SOFTWARE ROUNDTRIP",irq_register_probe());
    check("IRQ REGISTER STRESS: PASS",irq_soft_ticks==1024&&!irq_bad);
}
/* Temporary private branch, never map MMIO through the RAM allocator. Three
 * owned table frames, two UC device aliases and one read-only ACPI scratch PTE.
 * No VMM calls on this branch until it is removed. */
static void irq_map_init(void){
    irq_window=vmm_active.window;irq_root=vmm_resolve(NULL,vmm_active.root);
    unsigned slot=vmm_index(irq_window,4);
    check("IRQ PRIVATE SLOT: PASS",irq_root&&!irq_root[slot]&&vmm_active.table_pages==1&&!vmm_active.mapped);
    volatile uint64_t *t[3];unsigned n=0;
    for(;n<3;n++){
        if(vmm_take(NULL,&irq_frames[n])!=VMM_OK)break;
        t[n]=vmm_resolve(NULL,irq_frames[n]);if(!t[n]){(void)pmm_free_page(irq_frames[n]);break;}
        for(unsigned j=0;j<512;j++)t[n][j]=0;
    }
    if(n!=3){while(n)(void)pmm_free_page(irq_frames[--n]);check("IRQ TABLE ALLOCATION",0);}
    t[0][0]=irq_frames[1]|3;t[1][0]=irq_frames[2]|3;irq_pt=t[2];irq_root[slot]=irq_frames[0]|3;
    vmm_flush(NULL,irq_window);vmm_flush(NULL,irq_window+4096);vmm_flush(NULL,irq_window+8192);
}
static void irq_map_page(unsigned slot,uint64_t pa,uint64_t flags){
    check("IRQ MAP ADDRESS",slot<3&&!(pa&4095)&&pa<(UINT64_C(1)<<vmm_active.bits));
    irq_pt[slot]=pa|VMM_P|VMM_NX|flags;vmm_flush(NULL,irq_window+slot*4096);
}
static int irq_acpi_range(uint64_t pa,size_t n){
    if(!n||pa>UINT64_MAX-n)return 0;
    for(unsigned i=0;i<inventory.count;i++){
        const struct memory_region *r=&inventory.region[i];
        if((r->type==1||r->type==2||r->type==3||r->type==5)&&r->base<=pa&&pa+n<=r->end)return 1;
    }
    return 0;
}
static int irq_phys(void *ctx,uint64_t pa,void *out,size_t n){
    (void)ctx;uint8_t *dst=out;
    if(!irq_acpi_range(pa,n)||pa+n>(UINT64_C(1)<<vmm_active.bits))return 0;
    while(n){unsigned off=pa&4095;size_t len=4096-off;if(len>n)len=n;
        irq_map_page(2,pa&~UINT64_C(4095),0);
        const volatile uint8_t *src=(const volatile uint8_t *)(uintptr_t)(irq_window+8192+off);
        for(size_t j=0;j<len;j++)dst[j]=src[j];
        dst+=len;pa+=len;n-=len;
    }
    irq_pt[2]=0;vmm_flush(NULL,irq_window+8192);return 1;
}
static void irq_map_cleanup(void){
    irq_root[vmm_index(irq_window,4)]=0;
    for(unsigned i=0;i<3;i++)vmm_flush(NULL,irq_window+i*4096);
    for(unsigned i=3;i;i--)check("IRQ TABLE RELEASE",pmm_free_page(irq_frames[i-1])==PMM_OK);
    irq_pt=NULL;irq_lapic=NULL;irq_ioapic=NULL;
    check("IRQ FINAL ROOT ONLY: PASS",vmm_counts(&vmm_active));
}
static void irq_pic_init(void){
    irq_out(0x21,255);irq_out(0xa1,255);
    irq_out(0x20,0x11);irq_wait_io();irq_out(0xa0,0x11);irq_wait_io();
    irq_out(0x21,0x20);irq_wait_io();irq_out(0xa1,0x28);irq_wait_io();
    irq_out(0x21,4);irq_wait_io();irq_out(0xa1,2);irq_wait_io();
    irq_out(0x21,1);irq_wait_io();irq_out(0xa1,1);irq_wait_io();
    irq_out(0x21,255);irq_out(0xa1,255);
    check("M12.4 PIC MASKS: PASS",irq_in(0x21)==255&&irq_in(0xa1)==255);
}
#define IRQ_PIT_DIV 11932u
static void irq_pit_init(void){irq_out(0x43,0x34);irq_out(0x40,(uint8_t)IRQ_PIT_DIV);irq_out(0x40,(uint8_t)(IRQ_PIT_DIV>>8));}
__attribute__((noinline)) static int irq_until(volatile uint64_t *count,uint64_t target){
    uint64_t start=irq_tsc();
    __asm__ volatile("sti; nop":::"memory");
    while(*count<target&&irq_tsc()-start<UINT64_C(20000000000)&&!irq_bad)__asm__ volatile("pause":::"memory");
    __asm__ volatile("cli":::"memory");
    return *count>=target&&!irq_bad;
}
static void irq_run(void){
    unsigned a=0,b=0,c=0,d=0;
    check("M12.1 CPU INTERRUPT SUPPORT: PASS",__get_cpuid(1,&a,&b,&c,&d)&&(d&(1u<<9))&&(d&(1u<<4))&&!(irq_flags()&0x200));
    unsigned x2_supported=(c>>21)&1;
    irq_install();irq_pic_init();irq_map_init();
    check("IRQ RSDP RESPONSE: PASS",rsdp_request.response!=NULL&&hhdm_owned((const void *)rsdp_request.response,sizeof(*rsdp_request.response),5));
    uint64_t rsdp=(uint64_t)(uintptr_t)rsdp_request.response->address;
    if(rsdp>=inventory.hhdm&&vmm_canonical(rsdp))rsdp-=inventory.hhdm;
    struct irq_topology top;
    number("IRQ RSDP: ",rsdp);
    check("IRQ ACPI MADT: PASS",irq_acpi_discover(irq_phys,NULL,rsdp,&top));
    number("IRQ LAPIC PA: ",top.lapic);number("IRQ IOAPIC PA: ",top.ioapic);number("IRQ PIT GSI: ",top.pit_gsi);number("IRQ PIT FLAGS: ",top.pit_flags);
    uint64_t apicbase=read_msr(0x1b);irq_x2=(apicbase>>10)&1;
    check("IRQ APIC BASE: PASS",(apicbase&VMM_ADDR)==top.lapic&&(apicbase&(1u<<8))&&(!irq_x2||x2_supported)&&
          !pmm_is_managed_address(top.lapic)&&!pmm_is_managed_address(top.ioapic));
    /* PWT+PCD, PAT index 3 must be UC. Q35 MMIO only; never map arbitrary RAM. */
    check("IRQ PAT UC: PASS",((read_msr(0x277)>>24)&255)==0);
    irq_map_page(0,top.lapic,0x1a);irq_map_page(1,top.ioapic,0x1a);
    irq_lapic=(volatile uint32_t *)(uintptr_t)irq_window;irq_ioapic=(volatile uint32_t *)(uintptr_t)(irq_window+4096);
    struct vmm_translation tr;
    check("IRQ MMIO TRANSLATION: PASS",vmm_translate(&vmm_active,irq_window,&tr)==VMM_OK&&tr.physical==top.lapic&&tr.flags==(VMM_P|VMM_W|VMM_NX)&&
          vmm_translate(&vmm_active,irq_window+4096,&tr)==VMM_OK&&tr.physical==top.ioapic&&tr.flags==(VMM_P|VMM_W|VMM_NX));
    if(!(apicbase&(1u<<11)))irq_wrmsr(0x1b,apicbase|(1u<<11));
    unsigned id=irq_x2?irq_lread(0x20):irq_lread(0x20)>>24;
    uint32_t ver=irq_lread(0x30);unsigned maxlvt=(ver>>16)&255;
    check("IRQ LAPIC VERSION: PASS",id<=255&&(ver&255)>=0x10&&maxlvt>=5&&maxlvt<=7);
    irq_lwrite(0x80,0);irq_lwrite(0x320,1u<<16);irq_lwrite(0x380,0);
    irq_lwrite(0x330,1u<<16);irq_lwrite(0x340,1u<<16);irq_lwrite(0x350,1u<<16);irq_lwrite(0x360,1u<<16);irq_lwrite(0x370,1u<<16);
    if(maxlvt>=6)irq_lwrite(0x2f0,1u<<16);
    irq_lwrite(0xf0,0x1ff); /* software enable, spurious vector FF */
    check("M12.6 LOCAL APIC: PASS",(irq_lread(0xf0)&0x1ff)==0x1ff);
    number("IRQ APIC ID: ",id);number("IRQ APIC X2: ",irq_x2);
    uint32_t iover=irq_ioread(1);unsigned maxpin=(iover>>16)&255;
    unsigned pin=top.pit_gsi-top.gsi_base;
    check("IRQ IOAPIC VERSION: PASS",(iover&255)>=0x10&&maxpin<=119&&pin<=maxpin&&((irq_ioread(0)>>24)&15)==top.ioapic_id);
    for(unsigned i=0;i<=maxpin;i++)irq_iowrite(0x10+2*i,irq_ioread(0x10+2*i)|(1u<<16));
    for(unsigned i=0;i<=maxpin;i++)check("IRQ IOAPIC MASK",irq_ioread(0x10+2*i)&(1u<<16));
    /* Legacy virtual wire: PIT -> PIC -> LAPIC LINT0 ExtINT. */
    irq_lwrite(0x350,0x700);irq_pit_init();irq_out(0x21,0xfe);
    check("M12.5 PIC PIT DELIVERY: PASS",irq_until(&irq_pic_ticks,16));
    irq_out(0x21,255);irq_lwrite(0x350,1u<<16);
    uint64_t pic_done=irq_pic_ticks;number("IRQ PIC TICKS: ",pic_done);
    /* MADT IRQ0 override -> IOAPIC fixed physical destination. */
    uint32_t low=64;if((top.pit_flags&3)==3)low|=1u<<13;if(((top.pit_flags>>2)&3)==3)low|=1u<<15;
    irq_iowrite(0x11+2*pin,id<<24);irq_iowrite(0x10+2*pin,low);
    check("IRQ IOAPIC ROUTE READBACK: PASS",(irq_ioread(0x10+2*pin)&0x1a7ff)==low&&irq_ioread(0x11+2*pin)==id<<24);
    check("M12.7 IOAPIC PIT DELIVERY: PASS",irq_until(&irq_io_ticks,64)&&irq_pic_ticks==pic_done);
    /* Calibrate local timer against 16 PIT interrupts. Divide by 16. */
    irq_lwrite(0x3e0,3);irq_lwrite(0x380,UINT32_MAX);
    uint64_t before=irq_io_ticks;
    check("IRQ LAPIC CALIBRATION: PASS",irq_until(&irq_io_ticks,before+16));
    uint64_t delta=UINT32_MAX-irq_lread(0x390),elapsed=irq_io_ticks-before;
    uint32_t period=(uint32_t)(delta/elapsed);
    check("IRQ LAPIC PERIOD: PASS",period>=16&&period<UINT32_MAX/2);
    irq_lwrite(0x380,0);irq_iowrite(0x10+2*pin,low|(1u<<16));
    irq_lwrite(0x320,(1u<<17)|65);irq_lwrite(0x380,period);
    check("IRQ LAPIC TIMER DELIVERY: PASS",irq_until(&irq_apic_ticks,64));
    irq_lwrite(0x320,1u<<16);irq_lwrite(0x380,0);
    uint64_t pit=irq_pic_ticks+irq_io_ticks;
    number("IRQ IOAPIC TICKS: ",irq_io_ticks);number("IRQ LAPIC TICKS: ",irq_apic_ticks);number("IRQ LAPIC PERIOD: ",period);
    number("IRQ SOFTWARE TICKS: ",irq_soft_ticks);number("IRQ SPURIOUS TICKS: ",irq_spurious_ticks);number("IRQ BAD: ",irq_bad);
    number("IRQ PIT DIVISOR: ",IRQ_PIT_DIV);number("IRQ PIT ELAPSED MS: ",irq_pit_ms(pit,IRQ_PIT_DIV));
    check("M12.8 TIMER ACCOUNTING: PASS",pit>=96&&irq_pit_ms(pit,IRQ_PIT_DIV)>=960&&irq_apic_ticks>=64);
    for(unsigned i=0;i<=maxpin;i++)check("IRQ FINAL IOAPIC MASK",irq_ioread(0x10+2*i)&(1u<<16));
    check("M12.9 IRQ SAFETY: PASS",!irq_isr(64)&&!irq_isr(65)&&!irq_bad&&irq_soft_ticks==1024&&!(irq_flags()&0x600)&&irq_in(0x21)==255&&irq_in(0xa1)==255&&
          (irq_ioread(0x10+2*pin)&(1u<<16))&&(irq_lread(0x320)&(1u<<16)));
#if PROZEST_STAGE >= 15
    services_lapic_pa=top.lapic;services_lapic_period=period;
#endif
    irq_map_cleanup();
    log_write(LOG_OK,"PIC, IOAPIC and local APIC timer verified");
    log_write(LOG_OK,"Interrupt delivery tested; sources masked");
    qemu_debugcon_puts("M12.10 IRQ INTEGRATION: PASS\n");
}
