#include "mouse.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static unsigned long checks;
#define C(x) do{assert(x);checks++;}while(0)
static uint32_t pixels[26*40],original[26*40];
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
    for(size_t y=0;y<s.height;y++)for(size_t x=0;x<s.width;x++){
        c.x=x;c.y=y;mouse_show(&c,&s);C(c.visible&&s.pixels[y*s.stride+x]==0xffffff);
        mouse_show(&c,&s);mouse_move(&c,&s,1,1);C(c.x==x&&c.y==y);
        for(size_t j=0;j<26;j++)for(size_t i=0;i<40;i++)if(j==0||j==25||i>=32)C(pixels[j*40+i]==original[j*40+i]);
        mouse_hide(&c,&s);mouse_hide(&c,&s);C(!c.visible&&!memcmp(pixels,original,sizeof pixels));
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
    printf("M20 mouse assertions: %lu PASS; all 262144 signed movement combinations\n",checks);
}
