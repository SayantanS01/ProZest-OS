#include "mouse.h"
/* Standard PS/2 three-byte packet. The sign bits are the ninth bits. */
int mouse_decode(struct mouse_decoder *d,uint8_t b,struct mouse_event *e){
    if(!d||!e)return 0;
    if(d->used>2)d->used=0;
    if(!d->used&&!(b&8))return 0;
    d->packet[d->used++]=b;
    if(d->used!=3)return 0;
    d->used=0;
    unsigned f=d->packet[0];e->buttons=f&7;e->overflow=(f>>6)&3;
    e->dx=(int)d->packet[1]-((f&16)?256:0);
    e->dy=-((int)d->packet[2]-((f&32)?256:0));
    if(e->overflow)e->dx=e->dy=0;
    return 1;
}
static size_t clamp(size_t p,size_t limit,int d){
    if(p>=limit)p=limit-1;
    if(d<0){size_t n=(size_t)(-(int64_t)d);return n>p?0:p-n;}
    size_t n=(size_t)d;return n>=limit-p?limit-1:p+n;
}
/* Cursor movement must follow hide; the same surface is used for show/hide. */
void mouse_move(struct mouse_cursor *c,const struct desktop_surface *s,int dx,int dy){
    if(!c||c->visible||!desktop_surface_valid(s))return;
    c->x=clamp(c->x,s->width,dx);c->y=clamp(c->y,s->height,dy);
}
/* 1 = white border, 2 = dark interior, 0 = transparent. */
static unsigned shape(size_t x,size_t y){
    if(y<12){size_t edge=y/2;if(x>edge)return 0;return !x||x==edge||y==11?1:2;}
    if(x<3||x>5)return 0;
    return x==3||x==5||y==15?1:2;
}
void mouse_show(struct mouse_cursor *c,const struct desktop_surface *s){
    if(!c||c->visible||!desktop_surface_valid(s)||c->x>=s->width||c->y>=s->height)return;
    uint32_t white=desktop_rgb(s,255,255,255),black=desktop_rgb(s,0,0,0);
    for(size_t y=0;y<MOUSE_H&&y<s->height-c->y;y++)for(size_t x=0;x<MOUSE_W&&x<s->width-c->x;x++){
        unsigned a=shape(x,y);if(!a)continue;
        volatile uint32_t *p=s->pixels+(c->y+y)*s->stride+c->x+x;
        c->saved[y*MOUSE_W+x]=*p;*p=a==1?white:black;
    }
    c->visible=1;
}
void mouse_hide(struct mouse_cursor *c,const struct desktop_surface *s){
    if(!c||!c->visible||!desktop_surface_valid(s)||c->x>=s->width||c->y>=s->height)return;
    for(size_t y=0;y<MOUSE_H&&y<s->height-c->y;y++)for(size_t x=0;x<MOUSE_W&&x<s->width-c->x;x++)if(shape(x,y))
        s->pixels[(c->y+y)*s->stride+c->x+x]=c->saved[y*MOUSE_W+x];
    c->visible=0;
}
int mouse_hit(size_t x,size_t y){
    if(x<24||x>=168||y<90)return -1;
    size_t d=y-90;if(d>=4*48||d%48>=36)return -1;
    return (int)(d/48);
}
