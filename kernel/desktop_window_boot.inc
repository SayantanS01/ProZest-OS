#include "mouse_flame.h"
#include "window.h"
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
/* PIT channel 2 mode 0, speaker off, 59659 ticks ~= 50 ms. No IRQ changes. */
static void flame_timer_arm(void){
    uint8_t v=irq_in(0x61);irq_out(0x61,(uint8_t)(v&~3u));
    irq_out(0x43,0xb0);irq_out(0x42,(uint8_t)59659u);irq_out(0x42,(uint8_t)(59659u>>8));
    irq_out(0x61,(uint8_t)((v&~3u)|1u));
}
static void window_paint(struct mouse_cursor *c,const struct desktop_surface *s,enum desktop_view view,const struct wm_state *wm,const struct desktop_info *info,struct limine_framebuffer *fb){
    mouse_hide(c,s);check("M21 COMPOSITE: PASS",desktop_render(s,view,info,desktop_draw_text,fb)&&wm_render(s,wm,info,desktop_draw_text,fb));mouse_show(c,s);
}
static void desktop_run(const struct pzfs *fs,const struct pmm_stats *stats,struct shell_keys *keys){
    struct limine_framebuffer *fb=framebuffer_request.response->framebuffers[0];
    check("M19 FRAMEBUFFER PITCH: PASS",fb->pitch%4==0);
    struct desktop_surface s={fb->address,(size_t)fb->width,(size_t)fb->height,(size_t)(fb->pitch/4),fb->red_mask_shift,fb->green_mask_shift,fb->blue_mask_shift};
    struct desktop_info info={stats->managed_pages,stats->free_pages,fs};
    enum desktop_view view=DESKTOP_HOME;struct wm_state wm;
    struct mouse_decoder decoder={0};static struct mouse_cursor cursor={.x=140,.y=112};
    unsigned buttons=0,frames=0;int pressed=-1;
    check("M19 SURFACE: PASS",desktop_surface_valid(&s));check("M21 WINDOW INIT: PASS",wm_init(&wm,s.width,s.height));mouse_start();
    uint8_t speaker=irq_in(0x61);flame_timer_arm();
    window_paint(&cursor,&s,view,&wm,&info,fb);mouse_position(&cursor);qemu_debugcon_puts("M19 VIEW HOME\n");
    for(;;){
        if(irq_in(0x61)&0x20){mouse_animate(&cursor,&s);flame_timer_arm();if(frames<12&&++frames==12)qemu_debugcon_puts("M21 IDLE ANIMATION: PASS\n");}
        uint8_t status=irq_in(0x64);if(!(status&1)){__asm__ volatile("pause");continue;}
        uint8_t b=irq_in(0x60);if(status&0xc0){if(status&0x20){decoder.used=0;pressed=-1;buttons=0;wm_up(&wm);}continue;}
        int nav=-1,redraw=0;
        if(status&0x20){
            struct mouse_event e;if(!mouse_decode(&decoder,b,&e))continue;
            if(e.dx||e.dy){mouse_hide(&cursor,&s);mouse_move(&cursor,&s,e.dx,e.dy);if((buttons&1)&&wm_move(&wm,cursor.x,cursor.y)){redraw=1;qemu_debugcon_puts("M21 WINDOW MOVED\n");}if(!redraw)mouse_show(&cursor,&s);mouse_position(&cursor);}
            int hit=mouse_hit(cursor.x,cursor.y);
            if((e.buttons&1)&&!(buttons&1)){
                qemu_debugcon_puts("M20 LEFT DOWN\n");pressed=hit;
                if(hit<0){int action=wm_down(&wm,cursor.x,cursor.y);if(action){redraw=1;view=wm.top<0?DESKTOP_HOME:(enum desktop_view)(wm.top+1);qemu_debugcon_puts(action==3?"M21 WINDOW CLOSED\n":action==2?"M21 DRAG START\n":"M21 WINDOW FOCUS\n");}}
            }
            if(!(e.buttons&1)&&(buttons&1)){
                if(wm.drag>=0){qemu_debugcon_puts("M21 DRAG END\n");}wm_up(&wm);qemu_debugcon_puts("M20 LEFT UP\n");
                if(pressed>=0&&hit==pressed){nav=hit;number("M20 CLICK: ",(uint64_t)hit);}pressed=-1;
            }buttons=e.buttons;
        }else{unsigned key=shell_key(keys,b);if(key>='1'&&key<='4')nav=(int)(key-'1');}
        if(nav==3){mouse_hide(&cursor,&s);irq_out(0x61,(uint8_t)((irq_in(0x61)&~3u)|(speaker&3u)));mouse_stop();qemu_debugcon_puts("M19 TERMINAL SELECTED\nM19 COMPLETE\nM20 COMPLETE\nM21 COMPLETE\n");return;}
        if(nav>=0){view=(enum desktop_view)nav;if(nav==0)wm_home(&wm);else{check("M21 WINDOW OPEN: PASS",wm_open(&wm,nav-1));}redraw=1;
            qemu_debugcon_puts(view==DESKTOP_HOME?"M19 VIEW HOME\n":view==DESKTOP_SYSTEM?"M19 VIEW SYSTEM\n":"M19 VIEW FILES\n");
            if(view==DESKTOP_FILES){for(unsigned i=0;i<PZFS_SLOTS;i++){const uint8_t *e=fs->meta+16+32*i;if(e[0]){qemu_debugcon_puts("M19 FILE ");qemu_debugcon_puts((const char *)e);qemu_debugcon_putc('\n');}}}
        }
        if(redraw)window_paint(&cursor,&s,view,&wm,&info,fb);
    }
}
