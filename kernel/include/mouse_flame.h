#ifndef PROZEST_MOUSE_FLAME_H
#define PROZEST_MOUSE_FLAME_H
#include "desktop.h"
#define MOUSE_W 32u
#define MOUSE_H 32u
struct mouse_decoder { uint8_t packet[3]; unsigned used; };
struct mouse_event { int dx,dy; unsigned buttons,overflow; };
struct mouse_cursor { size_t x,y; unsigned visible,frame; uint32_t saved[MOUSE_W*MOUSE_H]; };
int mouse_decode(struct mouse_decoder *,uint8_t,struct mouse_event *);
void mouse_move(struct mouse_cursor *,const struct desktop_surface *,int,int);
void mouse_show(struct mouse_cursor *,const struct desktop_surface *);
void mouse_hide(struct mouse_cursor *,const struct desktop_surface *);
void mouse_animate(struct mouse_cursor *,const struct desktop_surface *);
int mouse_hit(size_t,size_t);
#endif
