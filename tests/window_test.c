#include "mouse_flame.h"
#include "window.h"
#include "prozest_flame_frames.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static unsigned long checks;
#define C(x) do{assert(x);checks++;}while(0)
static uint32_t pixels[26*40],original[26*40];
static uint32_t fbtest[482*644];
static void no_text(void *ctx,size_t x,size_t y,const char *s,uint32_t color){(void)ctx;(void)x;(void)y;(void)s;(void)color;}
int main(void){
    struct mouse_decoder d={0};struct mouse_event e={0};
    for(unsigned i=0;i<256;i++)if(!(i&8))C(!mouse_decode(&d,(uint8_t)i,&e)&&!d.used);
    for(int x=-256;x<=255;x++)for(int y=-256;y<=255;y++){
        unsigned buttons=(unsigned)(x+y)&7u;
        uint8_t f=(uint8_t)(8|buttons|(x<0?16:0)|(y<0?32:0));
        C(!mouse_decode(&d,f,&e));C(!mouse_decode(&d,(uint8_t)x,&e));
        C(mouse_decode(&d,(uint8_t)y,&e)&&e.dx==x&&e.dy==-y&&e.buttons==buttons&&!e.overflow&&!d.used);
    }
    for(unsigned f=1;f<=3;f++){C(!mouse_decode(&d,(uint8_t)(8|1|(f<<6)),&e));C(!mouse_decode(&d,255,&e));C(mouse_decode(&d,255,&e)&&!e.dx&&!e.dy&&e.overflow==f&&e.buttons==1);}
    d.used=99;C(!mouse_decode(&d,0,&e)&&!d.used);
    struct desktop_surface s={pixels+40,32,24,40,16,8,0};
    for(unsigned i=0;i<26*40;i++)original[i]=pixels[i]=0x123456u+i;
    struct mouse_cursor c={0};
    for(unsigned order=0;order<2;order++){
        s.r=order?0:16;s.b=order?16:0;
        for(size_t y=0;y<s.height;y++)for(size_t x=0;x<s.width;x++){
            c.x=x;c.y=y;mouse_show(&c,&s);C(c.visible);
            mouse_show(&c,&s);mouse_move(&c,&s,1,1);C(c.x==x&&c.y==y);
            for(size_t j=0;j<26;j++)for(size_t i=0;i<40;i++){
                uint32_t expected=original[j*40+i];
                int sx=(int)i-(int)x+6,sy=(int)j-1-(int)y+4;
                if(j>0&&j<25&&i<32&&sx>=0&&sx<32&&sy>=0&&sy<32){
                    uint32_t q=flame_frames[0][sy*32+sx];unsigned a=q&255u;
                    unsigned shifts[]={s.r,s.g,s.b};
                    for(unsigned k=0;k<3;k++){
                        unsigned old=(expected>>shifts[k])&255u,fg=(q>>(24-8*k))&255u;
                        unsigned v=(old*(255-a)+fg*a+127)/255;
                        expected=(expected&~(255u<<shifts[k]))|(v<<shifts[k]);
                    }
                }
                C(pixels[j*40+i]==expected);
            }
            mouse_hide(&c,&s);mouse_hide(&c,&s);C(!c.visible&&!memcmp(pixels,original,sizeof pixels));
        }
    }
    mouse_move(&c,&s,INT_MIN,INT_MIN);C(c.x==0&&c.y==0);
    mouse_move(&c,&s,INT_MAX,INT_MAX);C(c.x==31&&c.y==23);
    mouse_move(&c,&s,-7,-8);C(c.x==24&&c.y==15);
    c.x=SIZE_MAX;c.y=SIZE_MAX;mouse_move(&c,&s,0,0);C(c.x==31&&c.y==23);
    struct desktop_surface bad=s;bad.width=0;mouse_show(&c,&bad);C(!c.visible);mouse_move(&c,&bad,1,1);C(c.x==31&&c.y==23);
    for(size_t y=0;y<320;y++)for(size_t x=0;x<200;x++){
        int expected=-1;for(int i=0;i<4;i++)if(x>=24&&x<168&&y>=90u+48u*(unsigned)i&&y<126u+48u*(unsigned)i)expected=i;
        C(mouse_hit(x,y)==expected);
    }
    C(mouse_hit(SIZE_MAX,SIZE_MAX)==-1);C(mouse_hit(24,SIZE_MAX)==-1);
    for(unsigned k=0;k<FLAME_FRAMES;k++){
        for(unsigned i=0;i<19*32;i++)C(flame_frames[k][i]==flame_frames[0][i]);
        C(memcmp(flame_frames[k],flame_frames[(k+1)%FLAME_FRAMES],sizeof flame_frames[k]));
    }
    for(size_t y=0;y<s.height;y+=3)for(size_t x=0;x<s.width;x+=3){
        c.x=x;c.y=y;c.frame=0;mouse_show(&c,&s);
        for(unsigned k=0;k<FLAME_FRAMES;k++){mouse_animate(&c,&s);C(c.visible&&c.frame==(k+1)%FLAME_FRAMES&&c.x==x&&c.y==y);mouse_hide(&c,&s);C(!memcmp(pixels,original,sizeof pixels));mouse_show(&c,&s);}
        mouse_hide(&c,&s);C(!memcmp(pixels,original,sizeof pixels));
    }
    struct wm_state wm;C(!wm_init(&wm,639,480));C(wm_init(&wm,640,480));C(wm.top==-1);C(wm_open(&wm,0));C(wm_open(&wm,1));C(wm_hit(&wm,300,160)==1);C(wm_hit(&wm,230,110)==0);
    C(wm_down(&wm,300,152)==2);C(wm_move(&wm,320,172));C(wm.win[1].x==300&&wm.win[1].y==162);wm_up(&wm);C(!wm_move(&wm,0,0));
    C(wm_down(&wm,230,110)==2&&wm.top==0);wm_up(&wm);C(wm_down(&wm,520,110)==3&&wm.top==1);C(wm_down(&wm,605,172)==3&&wm.top==-1);
    C(wm_open(&wm,0));C(wm_down(&wm,230,110)==2);C(wm_move(&wm,SIZE_MAX,SIZE_MAX));C(wm.win[0].x==304&&wm.win[0].y==176);C(wm_move(&wm,0,0));C(wm.win[0].x==192&&wm.win[0].y==80);wm_home(&wm);C(wm.top==-1&&wm.drag==-1&&!wm.win[0].open&&!wm.win[1].open);
    struct desktop_surface full={fbtest+644,640,480,644,16,8,0};struct desktop_info info={100,99,0};
    for(size_t i=0;i<482*644;i++)fbtest[i]=0xdeadbeef;
    C(wm_init(&wm,640,480)&&wm_open(&wm,0)&&wm_open(&wm,1));C(desktop_render(&full,DESKTOP_FILES,&info,no_text,0)&&wm_render(&full,&wm,&info,no_text,0));
    C(full.pixels[102*644+220]==0x2c394e&&full.pixels[144*644+284]==0x42587a);
    C(wm_down(&wm,230,140)==1&&wm.top==0);C(wm_render(&full,&wm,&info,no_text,0));C(full.pixels[102*644+220]==0x42587a&&full.pixels[144*644+284]==0x253145);
    for(size_t i=0;i<644;i++)C(fbtest[i]==0xdeadbeef&&fbtest[481*644+i]==0xdeadbeef);
    for(size_t y=0;y<480;y++)for(size_t x=640;x<644;x++)C(full.pixels[y*644+x]==0xdeadbeef);
    printf("M21 window and flame assertions: %lu PASS; all 262144 signed movement combinations\n",checks);
}
