#include "desktop.h"
int desktop_surface_valid(const struct desktop_surface *s){
    if(!s||!s->pixels||((uintptr_t)s->pixels&3)||!s->width||!s->height||s->stride<s->width||s->stride>SIZE_MAX/4/s->height)return 0;
    if(s->stride*s->height*4>UINTPTR_MAX-(uintptr_t)s->pixels)return 0;
    if(s->r>24||s->g>24||s->b>24)return 0;
    uint32_t r=255u<<s->r,g=255u<<s->g,b=255u<<s->b;
    return !(r&g)&&!(r&b)&&!(g&b);
}
uint32_t desktop_rgb(const struct desktop_surface *s,uint8_t r,uint8_t g,uint8_t b){
    if(!desktop_surface_valid(s))return 0;
    return ((uint32_t)r<<s->r)|((uint32_t)g<<s->g)|((uint32_t)b<<s->b);
}
void desktop_rect(const struct desktop_surface *s,size_t x,size_t y,size_t w,size_t h,uint32_t color){
    if(!desktop_surface_valid(s)||x>=s->width||y>=s->height)return;
    if(w>s->width-x)w=s->width-x;
    if(h>s->height-y)h=s->height-y;
    for(size_t j=0;j<h;j++)for(size_t i=0;i<w;i++)s->pixels[(y+j)*s->stride+x+i]=color;
}
enum desktop_view desktop_select(enum desktop_view v,unsigned key){
    if(v==DESKTOP_TERMINAL)return v;
    return key>='1'&&key<='4'?(enum desktop_view)(key-'1'):v;
}
int desktop_render(const struct desktop_surface *s,enum desktop_view v,const struct desktop_info *info,desktop_text text,void *ctx){
    if(!desktop_surface_valid(s)||s->width<640||s->height<480||!info||!text||v<DESKTOP_HOME||v>DESKTOP_FILES)return 0;
    uint32_t bg=desktop_rgb(s,12,18,30),head=desktop_rgb(s,22,32,50),panel=desktop_rgb(s,30,43,63),white=desktop_rgb(s,235,243,255),muted=desktop_rgb(s,165,184,207);
    uint32_t accents[]={desktop_rgb(s,45,212,191),desktop_rgb(s,96,165,250),desktop_rgb(s,251,191,36)};
    const char *nav[]={"1 HOME","2 SYSTEM","3 FILES","4 TERMINAL"};
    desktop_rect(s,0,0,s->width,s->height,bg);
    desktop_rect(s,0,0,s->width,64,head);
    text(ctx,20,16,"PROZEST OS",white);text(ctx,20,38,"V0.0.7-RC13",muted);
    desktop_rect(s,16,80,160,s->height-136,head);
    for(unsigned i=0;i<4;i++){size_t y=96+i*48;if(i==(unsigned)v)desktop_rect(s,24,y-6,144,36,panel);text(ctx,32,y,nav[i],i==(unsigned)v?accents[v]:white);}
    desktop_rect(s,192,80,s->width-208,s->height-136,panel);
    desktop_rect(s,192,80,s->width-208,6,accents[v]);
    if(v==DESKTOP_HOME){
        text(ctx,208,104,"WELCOME TO PROZEST",white);
        text(ctx,208,146,"WINDOW DESKTOP",accents[v]);
        text(ctx,208,186,"CLICK OR PRESS 1-4",muted);
        text(ctx,208,226,"SYSTEM: MEMORY STATUS",white);
        text(ctx,208,256,"FILES: PZFS DIRECTORY",white);
        text(ctx,208,286,"TERMINAL: COMMAND SHELL",white);
    }
    desktop_rect(s,0,s->height-40,s->width,40,head);
    text(ctx,16,s->height-30,"1 HOME  2 SYSTEM  3 FILES  4 TERMINAL",muted);
    return 1;
}
