#include "desktop.h"
#include "shell.h"
static void desktop_draw_text(void *ctx,size_t x,size_t y,const char *s,uint32_t color){graphics_draw_string(ctx,s,x,y,color,2);}
static void desktop_run(const struct pzfs *fs,const struct pmm_stats *stats,struct shell_keys *keys){
    struct limine_framebuffer *fb=framebuffer_request.response->framebuffers[0];
    check("M19 FRAMEBUFFER PITCH: PASS",fb->pitch%4==0);
    struct desktop_surface s={fb->address,(size_t)fb->width,(size_t)fb->height,(size_t)(fb->pitch/4),fb->red_mask_shift,fb->green_mask_shift,fb->blue_mask_shift};
    struct desktop_info info={stats->managed_pages,stats->free_pages,fs};
    enum desktop_view view=DESKTOP_HOME;
    check("M19 SURFACE: PASS",desktop_surface_valid(&s));
    for(;;){
        check("M19 RENDER: PASS",desktop_render(&s,view,&info,desktop_draw_text,fb));
        qemu_debugcon_puts(view==DESKTOP_HOME?"M19 VIEW HOME\n":view==DESKTOP_SYSTEM?"M19 VIEW SYSTEM\n":"M19 VIEW FILES\n");
        if(view==DESKTOP_FILES){for(unsigned i=0;i<PZFS_SLOTS;i++){const uint8_t *e=fs->meta+16+32*i;if(e[0]){qemu_debugcon_puts("M19 FILE ");qemu_debugcon_puts((const char *)e);qemu_debugcon_putc('\n');}}}
        for(;;){
            uint8_t status=irq_in(0x64);if(!(status&1)){__asm__ volatile("pause");continue;}
            uint8_t b=irq_in(0x60);if(status&0xe0)continue;
            enum desktop_view next=desktop_select(view,shell_key(keys,b));
            if(next==DESKTOP_TERMINAL){qemu_debugcon_puts("M19 TERMINAL SELECTED\nM19 COMPLETE\n");return;}
            if(next!=view){view=next;break;}
        }
    }
}
