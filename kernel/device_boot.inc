/* Q35 bring-up: port I/O only, bounded polling, read-only PCI enumeration.
 * No DMA, PCI BAR programming or device IRQs. */
#include "device_core.h"
static void dev_out32(uint16_t p,uint32_t v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p):"memory");}
static uint32_t dev_in32(uint16_t p){uint32_t v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p):"memory");return v;}
static int dev_wait(uint16_t p,unsigned mask,unsigned want){
    uint64_t start=irq_tsc();while(irq_tsc()-start<UINT64_C(3000000000))if((irq_in(p)&mask)==want)return 1;return 0;
}
static uint32_t pci_read(unsigned b,unsigned d,unsigned f,unsigned r){uint32_t a;if(!pci_config_address(b,d,f,r,&a))return UINT32_MAX;dev_out32(0xcf8,a);return dev_in32(0xcfc);}
static void ps2_cmd(uint8_t c){check("PS2 INPUT READY",dev_wait(0x64,2,0));irq_out(0x64,c);}
static void ps2_data(uint8_t c){check("PS2 DATA READY",dev_wait(0x64,2,0));irq_out(0x60,c);}
static uint8_t ps2_read(void){check("PS2 OUTPUT READY",dev_wait(0x64,1,1));uint8_t s=irq_in(0x64);check("PS2 OUTPUT STATUS",!(s&0xe0));return irq_in(0x60);}
static void ps2_ack(uint8_t c){ps2_data(c);check("PS2 KEYBOARD ACK",ps2_read()==0xfa);}
static void device_run(void){
    check("M14.1 DRIVER CONTEXT: PASS",!(irq_flags()&0x200)&&irq_in(0x21)==255&&irq_in(0xa1)==255);
    const uint16_t u=0x3f8;irq_out(u+1,0);irq_out(u+3,0x80);irq_out(u,1);irq_out(u+1,0);irq_out(u+3,3);irq_out(u+2,7);irq_out(u+4,0x1e);
    irq_out(u+7,0x5a);check("M14.2 UART DETECTION: PASS",irq_in(u+7)==0x5a);
    for(unsigned i=0;i<64;i++){
        check("UART TX READY",dev_wait(u+5,0x20,0x20));irq_out(u,(uint8_t)(i^0xa5));
        check("UART RX READY",dev_wait(u+5,1,1));check("UART LOOPBACK BYTE",irq_in(u)==(uint8_t)(i^0xa5));
    }
    check("M14.3 UART LOOPBACK: PASS",dev_wait(u+5,0x40,0x40));irq_out(u+4,3);
    const char *msg="PROZEST M14 UART TX PASS\r\n";for(;*msg;msg++){check("UART OUTPUT READY",dev_wait(u+5,0x20,0x20));irq_out(u,(uint8_t)*msg);}
    check("M14.4 UART TX: PASS",dev_wait(u+5,0x40,0x40));
    unsigned count=0,host=0,vga=0,storage=0;
    for(unsigned d=0;d<32;d++){
        uint32_t id=pci_read(0,d,0,0);if((id&65535)==65535)continue;
        unsigned funcs=(pci_read(0,d,0,12)&(1u<<23))?8:1;
        for(unsigned f=0;f<funcs;f++){
            id=pci_read(0,d,f,0);if((id&65535)==65535)continue;
            uint32_t cls=pci_read(0,d,f,8)>>8;
            qemu_debugcon_puts("PCI DEVICE ");qemu_debugcon_hex64(d);qemu_debugcon_putc(' ');qemu_debugcon_hex64(f);qemu_debugcon_putc(' ');qemu_debugcon_hex64(id);qemu_debugcon_putc(' ');qemu_debugcon_hex64(cls);qemu_debugcon_putc('\n');
            count++;host+=(cls>>8)==0x0600;vga+=(cls>>8)==0x0300;storage+=(cls>>8)==0x0106;
        }
    }
    dev_out32(0xcf8,0);number("PCI DEVICE COUNT: ",count);
    check("M14.5 PCI ENUMERATION: PASS",count>=3&&host&&vga&&storage);
    check("M14.6 PCI CLASSIFICATION: PASS",host==1&&vga==1);
    ps2_cmd(0xad);ps2_cmd(0xa7);
    for(unsigned i=0;i<64&&(irq_in(0x64)&1);i++)(void)irq_in(0x60);
    check("PS2 DRAINED",!(irq_in(0x64)&1));
    ps2_cmd(0x20);uint8_t cfg=ps2_read();cfg=(uint8_t)((cfg&~3u)|0x60u);
    ps2_cmd(0x60);ps2_data(cfg);ps2_cmd(0xae);
    ps2_ack(0xf5);ps2_ack(0xf0);ps2_ack(2);ps2_ack(0xf4);
    check("M14.7 PS2 KEYBOARD INIT: PASS",1);
    struct key_decoder k={0};uint64_t start=irq_tsc();unsigned bytes=0;
    console_write("Press A to test keyboard input.\n");
    qemu_debugcon_puts("M14 KEYBOARD INPUT READY\n");
    while(!k.release&&irq_tsc()-start<UINT64_C(60000000000)&&bytes<64){
        if(irq_in(0x64)&1){uint8_t b=ps2_read();number("KEY BYTE: ",b);key_decode(&k,b);bytes++;}
        else __asm__ volatile("pause");
    }
    check("M14.8 KEYBOARD MAKE BREAK: PASS",k.press==1&&k.release==1&&!k.down&&!k.invalid);
    ps2_ack(0xf5);ps2_cmd(0xad);
    check("M14.9 DRIVER SAFETY: PASS",!(irq_flags()&0x200)&&irq_in(0x21)==255&&irq_in(0xa1)==255&&vmm_counts(&vmm_active));
    log_write(LOG_OK,"UART, PCI and PS/2 keyboard verified");
    check("M14.10 DRIVER INTEGRATION: PASS",1);qemu_debugcon_puts("M14 COMPLETE\n");
}
