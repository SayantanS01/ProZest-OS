#include "irq_platform.h"
#define ACPI_MAX 65536u
static uint16_t u16(const uint8_t *p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)|(uint32_t)u16(p+2)<<16;}
static uint64_t u64(const uint8_t *p){return (uint64_t)u32(p)|(uint64_t)u32(p+4)<<32;}
static int sig(const uint8_t *p,const char *s,unsigned n){for(unsigned i=0;i<n;i++)if(p[i]!=(uint8_t)s[i])return 0;return 1;}
static int sum_ok(const uint8_t *p,size_t n){uint8_t s=0;for(size_t i=0;i<n;i++)s=(uint8_t)(s+p[i]);return s==0;}
int irq_madt_parse(const uint8_t *p,size_t n,struct irq_topology *out){
    if(!p||!out||n<44||n>ACPI_MAX||!sig(p,"APIC",4)||u32(p+4)!=n||!sum_ok(p,n)||!(u32(p+40)&1))return 0;
    struct irq_topology t={.lapic=u32(p+36),.pit_gsi=0}; unsigned io=0,iso=0,override=0;
    for(size_t i=44;i<n;){
        if(n-i<2||p[i+1]<2||p[i+1]>n-i)return 0;
        unsigned len=p[i+1];
        if(p[i]==1){if(len!=12||io++)return 0;t.ioapic_id=p[i+2];t.ioapic=u32(p+i+4);t.gsi_base=u32(p+i+8);}
        if(p[i]==2){if(len!=10)return 0;if(p[i+2]==0&&p[i+3]==0){if(iso++)return 0;t.pit_gsi=u32(p+i+4);t.pit_flags=u16(p+i+8);}}
        if(p[i]==5){if(len!=12||override++)return 0;t.lapic=u64(p+i+4);}
        i+=len;
    }
    if(io!=1||!t.lapic||!t.ioapic||(t.lapic&4095)||(t.ioapic&4095)||t.lapic==t.ioapic||t.pit_gsi<t.gsi_base||
       (t.pit_flags&~15u)||(t.pit_flags&3)==2||((t.pit_flags>>2)&3)==2)return 0;
    *out=t;return 1;
}
/* Single BSP, static scratch avoids a 64 KiB kernel stack allocation. */
static uint8_t scratch[ACPI_MAX];
static int table(irq_phys_read rd,void *ctx,uint64_t pa,const char *name,uint32_t *len){
    if(!pa||!rd(ctx,pa,scratch,36)||!sig(scratch,name,4))return 0;
    uint32_t n=u32(scratch+4);if(n<36||n>ACPI_MAX||pa>UINT64_MAX-n||!rd(ctx,pa,scratch,n)||u32(scratch+4)!=n||!sig(scratch,name,4)||!sum_ok(scratch,n))return 0;
    *len=n;return 1;
}
int irq_acpi_discover(irq_phys_read rd,void *ctx,uint64_t pa,struct irq_topology *out){
    uint8_t rsdp[36];uint64_t entries[256];uint32_t n;
    if(!rd||!out||!pa||!rd(ctx,pa,rsdp,20)||!sig(rsdp,"RSD PTR ",8)||!sum_ok(rsdp,20))return 0;
    uint64_t root=u32(rsdp+16);unsigned stride=4;
    if(rsdp[15]>=2){if(!rd(ctx,pa,rsdp,36)||u32(rsdp+20)!=36||!sum_ok(rsdp,36))return 0;if(u64(rsdp+24)){root=u64(rsdp+24);stride=8;}}
    if(!table(rd,ctx,root,stride==8?"XSDT":"RSDT",&n)||(n-36)%stride||(n-36)/stride>256)return 0;
    unsigned count=(n-36)/stride;
    for(unsigned i=0;i<count;i++)entries[i]=stride==8?u64(scratch+36+i*stride):u32(scratch+36+i*stride);
    unsigned found=0;struct irq_topology t;
    for(unsigned i=0;i<count;i++){
        uint8_t h[4];if(!entries[i]||!rd(ctx,entries[i],h,4))return 0;
        if(sig(h,"APIC",4)){if(found++||!table(rd,ctx,entries[i],"APIC",&n)||!irq_madt_parse(scratch,n,&t))return 0;}
    }
    if(found!=1)return 0;
    *out=t;return 1;
}
uint64_t irq_pit_ms(uint64_t ticks,unsigned divisor){
    /* Bound arithmetic; saturate on overflow rather than wrap. */
    if(!divisor||divisor>65536)return 0;
    uint64_t scale=(uint64_t)divisor*1000, q=ticks/1193182,r=ticks%1193182;
    uint64_t tail=r*scale/1193182;
    if(q>(UINT64_MAX-tail)/scale)return UINT64_MAX;
    return q*scale+tail;
}
