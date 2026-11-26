#include "mouse.h"
#include "shell.h"
/* Controller interrupts stay masked. AUX status bit separates keyboard/mouse. */
static uint8_t mouse_response(void){
    uint64_t start=irq_tsc();
    while(irq_tsc()-start<UINT64_C(3000000000)){
        uint8_t s=irq_in(0x64);if(!(s&1)){__asm__ volatile("pause");continue;}
        uint8_t b=irq_in(0x60);if((s&0xe0)==0x20)return b;
    }
    check("M20 AUX RESPONSE",0);return 0;
}
static void mouse_command(uint8_t b){
    for(unsigned attempt=0;attempt<3;attempt++){
        ps2_cmd(0xd4);ps2_data(b);uint8_t r=mouse_response();
        if(r==0xfa)return;
        if(r!=0xfe)break;
    }
    check("M20 AUX ACK",0);
}
static void mouse_drain(void){
    for(unsigned i=0;i<64&&(irq_in(0x64)&1);i++)(void)irq_in(0x60);
    check("M20 AUX DRAIN",!(irq_in(0x64)&1));
}
static void mouse_start(void){
    ps2_cmd(0xad);mouse_drain();ps2_cmd(0xa8);
    mouse_command(0xff);check("M20 MOUSE BAT: PASS",mouse_response()==0xaa);
    check("M20 RESET ID: PASS",mouse_response()==0);
    mouse_command(0xf5);mouse_command(0xf6);mouse_command(0xf2);
    check("M20 STANDARD MOUSE: PASS",mouse_response()==0);
    mouse_command(0xf4);ps2_cmd(0xae);
    qemu_debugcon_puts("M20 MOUSE ENABLED: PASS\n");
}
static void mouse_stop(void){
    /* Disable the AUX controller port, so the terminal receives only keyboard input.
       Reporting is disabled on the controller, not reset at the device. */
    ps2_cmd(0xa7);mouse_drain();
    qemu_debugcon_puts("M20 AUX PORT DISABLED: PASS\n");
}
static void mouse_position(const struct mouse_cursor *c){
    qemu_debugcon_puts("M20 POINTER ");qemu_debugcon_hex64(c->x);qemu_debugcon_putc(' ');qemu_debugcon_hex64(c->y);qemu_debugcon_putc('\n');
}
static void desktop_draw_text(void *ctx,size_t x,size_t y,const char *s,uint32_t color){graphics_draw_string(ctx,s,x,y,color,2);}
static void desktop_run(const struct pzfs *fs,const struct pmm_stats *stats,struct shell_keys *keys){
    struct limine_framebuffer *fb=framebuffer_request.response->framebuffers[0];
    check("M19 FRAMEBUFFER PITCH: PASS",fb->pitch%4==0);
    struct desktop_surface s={fb->address,(size_t)fb->width,(size_t)fb->height,(size_t)(fb->pitch/4),fb->red_mask_shift,fb->green_mask_shift,fb->blue_mask_shift};
    struct desktop_info info={stats->managed_pages,stats->free_pages,fs};
    enum desktop_view view=DESKTOP_HOME;
    struct mouse_decoder decoder={0};struct mouse_cursor cursor={.x=140,.y=112};
    unsigned buttons=0;int pressed=-1;
    check("M19 SURFACE: PASS",desktop_surface_valid(&s));mouse_start();
    for(;;){
        mouse_hide(&cursor,&s);
        check("M19 RENDER: PASS",desktop_render(&s,view,&info,desktop_draw_text,fb));
        mouse_show(&cursor,&s);mouse_position(&cursor);
        qemu_debugcon_puts(view==DESKTOP_HOME?"M19 VIEW HOME\n":view==DESKTOP_SYSTEM?"M19 VIEW SYSTEM\n":"M19 VIEW FILES\n");
        if(view==DESKTOP_FILES){for(unsigned i=0;i<PZFS_SLOTS;i++){const uint8_t *e=fs->meta+16+32*i;if(e[0]){qemu_debugcon_puts("M19 FILE ");qemu_debugcon_puts((const char *)e);qemu_debugcon_putc('\n');}}}
        for(;;){
            uint8_t status=irq_in(0x64);if(!(status&1)){__asm__ volatile("pause");continue;}
            uint8_t b=irq_in(0x60);if(status&0xc0){if(status&0x20){decoder.used=0;pressed=-1;buttons=0;}continue;}
            enum desktop_view next=view;
            if(status&0x20){
                struct mouse_event e;if(!mouse_decode(&decoder,b,&e))continue;
                if(e.dx||e.dy){mouse_hide(&cursor,&s);mouse_move(&cursor,&s,e.dx,e.dy);mouse_show(&cursor,&s);mouse_position(&cursor);}
                int hit=mouse_hit(cursor.x,cursor.y);
                if((e.buttons&1)&&!(buttons&1)){pressed=hit;qemu_debugcon_puts("M20 LEFT DOWN\n");}
                if(!(e.buttons&1)&&(buttons&1)){
                    qemu_debugcon_puts("M20 LEFT UP\n");
                    if(pressed>=0&&hit==pressed){next=(enum desktop_view)hit;number("M20 CLICK: ",(uint64_t)hit);}
                    pressed=-1;
                }
                buttons=e.buttons;
            }else next=desktop_select(view,shell_key(keys,b));
            if(next==DESKTOP_TERMINAL){mouse_hide(&cursor,&s);mouse_stop();qemu_debugcon_puts("M19 TERMINAL SELECTED\nM19 COMPLETE\nM20 COMPLETE\n");return;}
            if(next!=view){view=next;break;}
        }
    }
}
