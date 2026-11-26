#include "window.h"
static size_t bound(size_t n,size_t lo,size_t hi){return n<lo?lo:n>hi?hi:n;}
int wm_init(struct wm_state *m,size_t w,size_t h){
    if(!m||w<640||h<480)return 0;
    *m=(struct wm_state){.width=w,.height=h,.win={{216,100,0},{280,142,0}},.top=-1,.drag=-1};return 1;
}
int wm_open(struct wm_state *m,int id){if(!m||id<0||id>1)return 0;m->win[id].open=1;m->top=id;m->drag=-1;return 1;}
void wm_home(struct wm_state *m){if(m){m->win[0].open=m->win[1].open=0;m->top=m->drag=-1;}}
static int inside(const struct wm_window *w,size_t x,size_t y){return w->open&&x>=w->x&&x-w->x<WM_W&&y>=w->y&&y-w->y<WM_H;}
int wm_hit(const struct wm_state *m,size_t x,size_t y){
    if(!m||m->top<0||m->top>1)return -1;
    if(inside(&m->win[m->top],x,y))return m->top;
    int other=1-m->top;return inside(&m->win[other],x,y)?other:-1;
}
/* 0 no hit; 1 focus; 2 title drag; 3 close. */
int wm_down(struct wm_state *m,size_t x,size_t y){
    int id=wm_hit(m,x,y);if(id<0)return 0;
    struct wm_window *w=&m->win[id];m->top=id;m->drag=-1;
    size_t dx=x-w->x,dy=y-w->y;
    if(dy<WM_TITLE&&dx>=WM_W-30){w->open=0;m->top=m->win[1-id].open?1-id:-1;return 3;}
    if(dy<WM_TITLE){m->drag=id;m->ox=dx;m->oy=dy;return 2;}
    return 1;
}
int wm_move(struct wm_state *m,size_t x,size_t y){
    if(!m||m->drag<0||m->drag>1||!m->win[m->drag].open)return 0;
    struct wm_window *w=&m->win[m->drag];
    size_t nx=bound(x>=m->ox?x-m->ox:0,192,m->width-16-WM_W),ny=bound(y>=m->oy?y-m->oy:0,80,m->height-56-WM_H);
    int changed=nx!=w->x||ny!=w->y;w->x=nx;w->y=ny;return changed;
}
void wm_up(struct wm_state *m){if(m)m->drag=-1;}
static void dec(char b[21],uint64_t n){unsigned i=20;b[i]=0;do{b[--i]=(char)('0'+n%10);n/=10;}while(n);unsigned j=0;while(i<=20)b[j++]=b[i++];}
static void paint(const struct desktop_surface *s,const struct wm_state *m,int id,const struct desktop_info *info,desktop_text text,void *ctx){
    const struct wm_window *w=&m->win[id];if(!w->open)return;
    size_t x=w->x,y=w->y;
    uint32_t body=desktop_rgb(s,37,49,69),bar=desktop_rgb(s,m->top==id?66:44,m->top==id?88:57,m->top==id?122:78),white=desktop_rgb(s,235,243,255),accent=desktop_rgb(s,125,230,245);
    desktop_rect(s,x,y,WM_W,WM_H,body);desktop_rect(s,x,y,WM_W,WM_TITLE,bar);
    desktop_rect(s,x+WM_W-30,y,30,WM_TITLE,desktop_rgb(s,155,56,76));
    text(ctx,x+8,y+8,id?"FILES":"SYSTEM",white);text(ctx,x+WM_W-22,y+8,"X",white);
    if(!id){char b[21];text(ctx,x+12,y+48,"X86-64 / PZFS",accent);text(ctx,x+12,y+82,"MANAGED PAGES",white);dec(b,info->managed);text(ctx,x+12,y+102,b,accent);text(ctx,x+12,y+138,"FREE PAGES",white);dec(b,info->free);text(ctx,x+12,y+158,b,accent);}
    else{unsigned row=0;if(info->fs&&info->fs->mounted)for(unsigned i=0;i<PZFS_SLOTS;i++){
        const uint8_t *e=info->fs->meta+16+32*i;if(!e[0])continue;
        char name[PZFS_NAME+1];unsigned j=0;for(;j<PZFS_NAME&&e[j];j++)name[j]=(e[j]>=32&&e[j]<=126)?(char)e[j]:'?';name[j]=0;
        text(ctx,x+12,y+42+row*22,name,white);row++;
    }if(!row)text(ctx,x+12,y+42,"NO FILES",accent);}
}
int wm_render(const struct desktop_surface *s,const struct wm_state *m,const struct desktop_info *info,desktop_text text,void *ctx){
    if(!m||!info||!text||!desktop_surface_valid(s)||s->width!=m->width||s->height!=m->height||s->width<640||s->height<480)return 0;
    if(m->top < -1||m->top>1)return 0;
    for(int i=0;i<2;i++)if(m->win[i].open&&(m->win[i].x<192||m->win[i].x>s->width-16-WM_W||m->win[i].y<80||m->win[i].y>s->height-56-WM_H))return 0;
    if(m->top>=0){paint(s,m,1-m->top,info,text,ctx);paint(s,m,m->top,info,text,ctx);}return 1;
}
