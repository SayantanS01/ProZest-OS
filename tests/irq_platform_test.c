#include "irq_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t mem[8192];
static void put(uint8_t *p,uint64_t x,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(x>>(8*i));}
static void sum(uint8_t *p,size_t n,unsigned at){p[at]=0;uint8_t s=0;for(size_t i=0;i<n;i++)s=(uint8_t)(s+p[i]);p[at]=(uint8_t)-s;}
static int readmem(void *ctx,uint64_t p,void *out,size_t n){(void)ctx;if(p>sizeof(mem)||n>sizeof(mem)-p)return 0;memcpy(out,mem+p,n);return 1;}
static void fixture(void){memset(mem,0,sizeof(mem));uint8_t *r=mem+64,*x=mem+256,*m=mem+4090;
    memcpy(r,"RSD PTR ",8);r[15]=2;put(r+20,36,4);put(r+24,256,8);sum(r,20,8);sum(r,36,32);
    memcpy(x,"XSDT",4);put(x+4,44,4);put(x+36,4090,8);sum(x,44,9);
    memcpy(m,"APIC",4);put(m+4,66,4);put(m+36,0xfee00000,4);put(m+40,1,4);
    m[44]=1;m[45]=12;put(m+48,0xfec00000,4);m[56]=2;m[57]=10;put(m+60,2,4);sum(m,66,9);
}
int main(void){struct irq_topology t;fixture();C(irq_acpi_discover(readmem,NULL,64,&t));C(t.lapic==0xfee00000&&t.ioapic==0xfec00000&&t.pit_gsi==2&&t.pit_flags==0);
    for(unsigned i=0;i<36;i++){fixture();mem[64+i]^=1;C(!irq_acpi_discover(readmem,NULL,64,&t));}
    for(unsigned i=0;i<44;i++){fixture();mem[256+i]^=1;C(!irq_acpi_discover(readmem,NULL,64,&t));}
    for(unsigned i=0;i<66;i++){fixture();mem[4090+i]^=1;C(!irq_acpi_discover(readmem,NULL,64,&t));}
    for(unsigned n=0;n<66;n++){fixture();C(!irq_madt_parse(mem+4090,n,&t));}
    for(unsigned len=0;len<256;len++){fixture();mem[4090+45]=(uint8_t)len;sum(mem+4090,66,9);C(irq_acpi_discover(readmem,NULL,64,&t)==(len==12));}
    for(unsigned f=0;f<256;f++){fixture();put(mem+4090+64,f,2);sum(mem+4090,66,9);int valid=f<16&&(f&3)!=2&&((f>>2)&3)!=2;C(irq_acpi_discover(readmem,NULL,64,&t)==valid);}
    fixture();put(mem+4090+48,0xfee00000,4);sum(mem+4090,66,9);C(!irq_acpi_discover(readmem,NULL,64,&t));
    fixture();put(mem+4090+52,3,4);sum(mem+4090,66,9);C(!irq_acpi_discover(readmem,NULL,64,&t));
    fixture();C(!irq_acpi_discover(readmem,NULL,UINT64_MAX,&t));C(!irq_acpi_discover(NULL,NULL,64,&t));C(!irq_acpi_discover(readmem,NULL,64,NULL));
    fixture();uint8_t *r=mem+64,*x=mem+256; r[15]=0;put(r+16,256,4);sum(r,20,8);memcpy(x,"RSDT",4);put(x+4,40,4);put(x+36,4090,4);sum(x,40,9);C(irq_acpi_discover(readmem,NULL,64,&t));
    for(unsigned div=1;div<=65536;div+=17)for(unsigned ticks=0;ticks<100000;ticks+=997){C(irq_pit_ms(ticks,div)==(uint64_t)ticks*div*1000/1193182);}
    C(irq_pit_ms(1,0)==0);C(irq_pit_ms(1,65537)==0);C(irq_pit_ms(UINT64_MAX,65536)==UINT64_MAX);
    printf("M12 platform host assertions: %u/%u PASS\n",checks,checks);return 0;
}
