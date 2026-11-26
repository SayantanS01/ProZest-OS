#include "desktop.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned long checks;
#define C(x) do{assert(x);checks++;}while(0)
static uint32_t pixels[482*648];
static void text(void *ctx,size_t x,size_t y,const char *s,uint32_t color){(void)ctx;(void)color;C(x+strlen(s)*12<=640&&y+16<=480);}
int main(void){
    struct desktop_surface s={pixels+648,640,480,648,16,8,0};
    C(desktop_surface_valid(&s));C(desktop_rgb(&s,1,2,3)==0x010203);
    struct desktop_surface bad=s;bad.r=8;C(!desktop_surface_valid(&bad));bad=s;bad.stride=639;C(!desktop_surface_valid(&bad));bad=s;bad.height=SIZE_MAX;C(!desktop_surface_valid(&bad));C(!desktop_surface_valid(0));
    for(unsigned i=0;i<sizeof pixels/sizeof *pixels;i++)pixels[i]=0xdeadbeef;
    desktop_rect(&s,638,478,SIZE_MAX,SIZE_MAX,0x123456);
    for(size_t y=0;y<482;y++)for(size_t x=0;x<648;x++)C(pixels[y*648+x]==((y==479||y==480)&&(x==638||x==639)?0x123456:0xdeadbeef));
    desktop_rect(&s,SIZE_MAX,0,1,1,0);desktop_rect(&s,0,SIZE_MAX,1,1,0);
    struct pzfs fs={0};fs.mounted=1;for(unsigned i=0;i<PZFS_SLOTS;i++)memcpy(fs.meta+16+32*i,"sample.txt",11);
    struct desktop_info info={UINT64_MAX,1,&fs};
    for(unsigned v=0;v<3;v++){
        C(desktop_render(&s,(enum desktop_view)v,&info,text,0));
        C(s.pixels[82*s.stride+200]==((uint32_t[]){0x2dd4bf,0x60a5fa,0xfbbf24})[v]);
        for(size_t y=0;y<482;y++)for(size_t x=0;x<648;x++)if(y==0||y==481||x>=640)C(pixels[y*648+x]==0xdeadbeef);
    }
    C(!desktop_render(&s,DESKTOP_TERMINAL,&info,text,0));
    for(unsigned v=0;v<4;v++)for(unsigned k=0;k<256;k++)C(desktop_select((enum desktop_view)v,k)==(enum desktop_view)(v!=3&&k>='1'&&k<='4'?k-'1':v));
    printf("M19 desktop assertions: %lu PASS\n",checks);
}
