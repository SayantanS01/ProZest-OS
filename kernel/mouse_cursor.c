#include "mouse_cursor.h"
#include "prozest_cursor_pixels.h"
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
/* Coordinates are the click hotspot. Clip on all four edges without signed overflow. */
static int coord(size_t p,size_t limit,size_t i,size_t hot,size_t *out){
    if(i<hot){size_t d=hot-i;if(p<d)return 0;*out=p-d;}
    else{size_t d=i-hot;if(d>=limit-p)return 0;*out=p+d;}
    return 1;
}
static uint32_t blend(uint32_t old,uint32_t rgba,const struct desktop_surface *s){
    unsigned a=rgba&255u;
    unsigned r=(((rgba>>24)&255u)*a+((old>>s->r)&255u)*(255u-a)+127u)/255u;
    unsigned g=(((rgba>>16)&255u)*a+((old>>s->g)&255u)*(255u-a)+127u)/255u;
    unsigned b=(((rgba>>8)&255u)*a+((old>>s->b)&255u)*(255u-a)+127u)/255u;
    uint32_t mask=(255u<<s->r)|(255u<<s->g)|(255u<<s->b);
    return (old&~mask)|(r<<s->r)|(g<<s->g)|(b<<s->b);
}
void mouse_show(struct mouse_cursor *c,const struct desktop_surface *s){
    if(!c||c->visible||!desktop_surface_valid(s)||c->x>=s->width||c->y>=s->height)return;
    for(size_t y=0;y<MOUSE_H;y++)for(size_t x=0;x<MOUSE_W;x++){
        size_t xx,yy,i=y*MOUSE_W+x;uint32_t rgba=prozest_cursor_pixels[i];
        if(!(rgba&255u)||!coord(c->x,s->width,x,PROZEST_CURSOR_HOT_X,&xx)||!coord(c->y,s->height,y,PROZEST_CURSOR_HOT_Y,&yy))continue;
        volatile uint32_t *p=s->pixels+yy*s->stride+xx;
        c->saved[i]=*p;*p=blend(c->saved[i],rgba,s);
    }
    c->visible=1;
}
void mouse_hide(struct mouse_cursor *c,const struct desktop_surface *s){
    if(!c||!c->visible||!desktop_surface_valid(s)||c->x>=s->width||c->y>=s->height)return;
    for(size_t y=0;y<MOUSE_H;y++)for(size_t x=0;x<MOUSE_W;x++){
        size_t xx,yy,i=y*MOUSE_W+x;
        if((prozest_cursor_pixels[i]&255u)&&coord(c->x,s->width,x,PROZEST_CURSOR_HOT_X,&xx)&&coord(c->y,s->height,y,PROZEST_CURSOR_HOT_Y,&yy))
            s->pixels[yy*s->stride+xx]=c->saved[i];
    }
    c->visible=0;
}
int mouse_hit(size_t x,size_t y){
    if(x<24||x>=168||y<90)return -1;
    size_t d=y-90;if(d>=4*48||d%48>=36)return -1;
    return (int)(d/48);
}
