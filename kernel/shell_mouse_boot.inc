#include "shell.h"
extern void console_shell_clear(void),console_shell_backspace(void);
static void shell_output(void *ctx,const char *s){(void)ctx;console_write(s);qemu_debugcon_puts(s);}
static void shell_clear(void *ctx){(void)ctx;console_shell_clear();}
static void shell_run(void){
    struct pzfs fs;struct pzfs_io disk={0,ata_read,ata_write};struct pmm_stats stats;
    check("M18 STORAGE CONTEXT: PASS",ata_ready&&pzfs_mount(&fs,disk)==PZFS_OK&&pmm_get_stats(&stats)==PMM_OK);
    struct shell_io io={0,shell_output,shell_clear,stats.managed_pages,stats.free_pages};
    struct shell_keys keys={0};struct shell_line line={0};
    ps2_cmd(0xad);ps2_cmd(0xa7);for(unsigned i=0;i<64&&(irq_in(0x64)&1);i++)(void)irq_in(0x60);
    check("M18 PS2 DRAIN: PASS",!(irq_in(0x64)&1));
    ps2_cmd(0x20);uint8_t cfg=ps2_read();cfg=(uint8_t)((cfg&~3u)|0x60u);
    ps2_cmd(0x60);ps2_data(cfg);ps2_cmd(0xae);ps2_ack(0xf5);ps2_ack(0xf0);ps2_ack(2);ps2_ack(0xf4);
    desktop_run(&fs,&stats,&keys);
    console_shell_clear();shell_output(0,"ProZest OS v0.0.7-rc12\nType help for commands.\n");
    qemu_debugcon_puts("M18 SHELL READY\n");
    for(;;){
        console_write("pz> ");qemu_debugcon_puts("SHELL PROMPT\n");
        line.length=line.overflow=0;line.text[0]=0;
        for(;;){
            uint8_t status=irq_in(0x64);if(!(status&1)){__asm__ volatile("pause");continue;}
            uint8_t b=irq_in(0x60);if(status&0xe0)continue;
            unsigned c=shell_key(&keys,b),event=shell_feed(&line,c);
            if(event==1){char s[2]={(char)c,0};console_write(s);}
            else if(event==2)console_shell_backspace();
            else if(event==3||event==4){console_newline();if(event==4){shell_output(0,"error: line too long\n");break;}
                qemu_debugcon_puts("SHELL CMD ");qemu_debugcon_puts(line.text);qemu_debugcon_putc('\n');
                if(shell_exec(&fs,&io,line.text))goto done;
                break;}
        }
    }
done:
    ps2_ack(0xf5);ps2_cmd(0xad);ata_ready=0;
    check("M18 SHELL CLEANUP: PASS",vmm_counts(&vmm_active)&&!(irq_flags()&0x200));
    qemu_debugcon_puts("M18 COMPLETE\n");
}
